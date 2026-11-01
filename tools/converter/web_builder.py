"""Private loopback front end for the existing local conversion pipeline."""
from __future__ import annotations

import base64
import binascii
from collections import deque
import errno
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import signal
import subprocess
import sys
import threading
from urllib.request import urlopen
import webbrowser

ROOT = Path(__file__).resolve().parents[2]
ASSETS = Path(__file__).resolve().parent / 'web'
MAX_FILE = 16 * 1024 * 1024
MAX_BODY = (MAX_FILE * 4 // 3) + 4096


def acquire_workspace_lock(root: Path):
    """Hold an OS lock across all converter stages; closing releases ownership.

    The persistent file is never removed: operating-system ownership, rather
    than stale PID files, also makes process crashes release the workspace.
    """
    path = root / 'build' / 'builder.lock'
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open('a+b')
    try:
        if os.name == 'nt':
            import msvcrt
            if handle.seek(0, os.SEEK_END) == 0:
                handle.write(b'\0')
                handle.flush()
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as exc:
        handle.close()
        if exc.errno in (errno.EACCES, errno.EAGAIN):
            raise RuntimeError('Another builder is building in this workspace. Wait for it to finish, then try again.') from None
        raise
    return handle


def verified_patch(root: Path, download=None, expected_hash=None) -> bytes:
    """Fetch only the fixed source patch and verify every cached/downloaded byte."""
    import build
    digest = expected_hash or build.REDUX_PATCH_SHA256
    cache = root / 'build' / 'builder_cache' / 'Zelda1_Redux.ips'
    if cache.is_file():
        data = cache.read_bytes()
        if hashlib.sha256(data).hexdigest() == digest:
            return data
    if download is None:
        def download(url):
            import ssl
            import certifi
            context = ssl.create_default_context(cafile=certifi.where())
            with urlopen(url, timeout=30, context=context) as response:
                data = response.read(MAX_FILE + 1)
                if len(data) > MAX_FILE:
                    raise ValueError('The Redux patch download is too large.')
                return data
    data = download(build.REDUX_DOWNLOAD)
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError('The Redux patch did not pass verification. Try again or choose the v3.3.3 patch in Advanced.')
    cache.parent.mkdir(parents=True, exist_ok=True)
    temporary = cache.with_suffix('.tmp')
    temporary.write_bytes(data)
    temporary.replace(cache)
    return data


class BuilderApplication:
    def __init__(self, root=ROOT, command_factory=None, patch_provider=None, prepare=None):
        self.root = Path(root).resolve()
        self.token = secrets.token_urlsafe(32)
        self.inputs = self.root / 'build' / 'builder_inputs' / secrets.token_hex(12)
        self.inputs.mkdir(parents=True, exist_ok=True)
        self.output_dir = self.root / 'build' / 'builder_output'
        self.command_factory = command_factory or self._command
        self.patch_provider = patch_provider or (lambda: verified_patch(self.root))
        self.prepare = prepare
        self.lock = threading.RLock()
        self.state = dict(status='idle', stage='Choose your Zelda NES ROM', progress=0, error='')
        self.logs = deque(maxlen=1200)
        self.rom = None
        self.patch = None
        self.output = None
        self.process = None
        self.worker = None
        self.cancelled = threading.Event()

    def _command(self, rom, patch, output):
        return [sys.executable, str(self.root / 'tools' / 'converter' / 'build.py'),
                str(rom), '--redux-patch', str(patch), '--output', str(output)]

    def snapshot(self):
        with self.lock:
            return {**self.state, 'rom_ready': self.rom is not None, 'patch_ready': self.patch is not None,
                    'logs': list(self.logs), 'download_ready': self.state['status'] == 'success'}

    def upload(self, payload):
        kind = payload.get('kind')
        if kind not in ('rom', 'patch'):
            raise ValueError('Choose a ROM or Redux patch file.')
        try:
            data = base64.b64decode(payload.get('data', ''), validate=True)
        except (ValueError, TypeError, binascii.Error):
            raise ValueError('The uploaded file could not be read.') from None
        if not data or len(data) > MAX_FILE:
            raise ValueError('Choose a file smaller than 16 MB.')
        if kind == 'rom' and (len(data) != 131088 or not data.startswith(b'NES\x1a')):
            raise ValueError('Choose the original 128 KB Zelda (USA) PRG0 .nes ROM, including its iNES header.')
        # Supplied file names never enter a filesystem path.
        path = self.inputs / ('zelda.nes' if kind == 'rom' else 'redux-patch.ips')
        with self.lock:
            if self.state['status'] == 'running':
                raise RuntimeError('A build is already running.')
            path.write_bytes(data)
            if kind == 'rom':
                self.rom = path
            else:
                self.patch = path

    def start(self):
        with self.lock:
            if self.state['status'] == 'running':
                raise RuntimeError('A build is already running.')
            if self.rom is None:
                raise ValueError('Choose your Zelda NES ROM first.')
            self.cancelled.clear()
            self.logs.clear()
            self.output = None
            self.state.update(status='running', stage='Preparing your builder', progress=1, error='')
            self.worker = threading.Thread(target=self._run, daemon=True)
            self.worker.start()

    def _run(self):
        target = self.output_dir / (secrets.token_hex(12) + '.md')
        workspace_lock = None
        try:
            workspace_lock = acquire_workspace_lock(self.root)
            if self.prepare:
                self.prepare(self.root, self._log)
            if self.cancelled.is_set():
                return
            patch = self.patch
            if patch is None:
                with self.lock:
                    self.state['stage'] = 'Getting the verified Redux patch'
                patch = self.inputs / 'verified-redux.ips'
                patch.write_bytes(self.patch_provider())
            if self.cancelled.is_set():
                return
            self.output_dir.mkdir(parents=True, exist_ok=True)
            options = dict(cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, encoding='utf-8', errors='replace', bufsize=1,
                           env={**os.environ, 'PYTHONUNBUFFERED': '1', 'PYTHONIOENCODING': 'utf-8'})
            if os.name == 'nt':
                options['creationflags'] = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
            else:
                options['start_new_session'] = True
            with self.lock:
                if self.cancelled.is_set():
                    return
                self.process = subprocess.Popen(self.command_factory(self.rom, patch, target), **options)
            for line in self.process.stdout:
                self._log(line.rstrip())
            result = self.process.wait()
            if self.cancelled.is_set():
                return
            if result != 0:
                raise ValueError(f'Build stopped (exit {result}). Open Build details for the cause, then try again.')
            if not target.is_file() or target.is_symlink() or target.stat().st_size == 0:
                raise ValueError('The builder finished without creating a ROM. Open Build details for the cause.')
            with self.lock:
                self.output = target
                self.state.update(status='success', stage='Your Genesis ROM is ready', progress=100)
        except Exception as exc:
            with self.lock:
                self.logs.append(str(exc)[:4000])
                self.state.update(status='error', stage='Build needs attention', error=str(exc)[:1000])
        finally:
            try:
                with self.lock:
                    if self.process and self.process.stdout:
                        self.process.stdout.close()
                    self.process = None
                    if self.cancelled.is_set():
                        self.output = None
                        self.state.update(status='cancelled', stage='Build cancelled', error='')
                        target.unlink(missing_ok=True)
            finally:
                if workspace_lock is not None:
                    workspace_lock.close()

    def _log(self, line):
        with self.lock:
            self.logs.append(str(line)[:4000])
            match = re.match(r'@@step (\d+)/(\d+) (.+)', str(line))
            if match:
                step, total, label = match.groups()
                self.state.update(stage=label, progress=min(98, int(step) * 95 // max(1, int(total))))

    def cancel(self):
        with self.lock:
            if self.state['status'] != 'running':
                return
            self.cancelled.set()
            process = self.process
            self.state['stage'] = 'Cancelling build'
        if process and process.poll() is None:
            try:
                if os.name == 'nt':
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
                else:
                    os.killpg(process.pid, signal.SIGKILL)
            except (ProcessLookupError, OSError, subprocess.TimeoutExpired):
                process.kill()

    def close(self):
        self.cancel()
        if self.worker:
            self.worker.join(timeout=35)


def create_server(app, host='127.0.0.1', port=0):
    if host != '127.0.0.1':
        raise ValueError('The builder may only listen on 127.0.0.1.')

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.0'

        def log_message(self, *args):
            pass

        def reply(self, status, body, content_type='application/json', disposition=None):
            if isinstance(body, (dict, list)):
                body = json.dumps(body).encode()
            if isinstance(body, str):
                body = body.encode()
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
            if disposition:
                self.send_header('Content-Disposition', disposition)
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def authorized(self):
            expected = f'127.0.0.1:{self.server.server_address[1]}'
            origin = self.headers.get('Origin')
            if self.headers.get('Host') != expected or (origin is not None and origin != 'http://' + expected):
                self.reply(403, {'error': 'This builder accepts requests only from its local page.'})
                return False
            if self.path.startswith('/api/') and not secrets.compare_digest(self.headers.get('X-Builder-Token', ''), app.token):
                self.reply(403, {'error': 'Reopen the builder using its launch link.'})
                return False
            return True

        def do_GET(self):
            if not self.authorized():
                return
            if self.path == '/api/status':
                self.reply(200, app.snapshot())
            elif self.path == '/api/log':
                self.reply(200, '\n'.join(app.snapshot()['logs']), 'text/plain; charset=utf-8', 'attachment; filename="builder.log"')
            elif self.path == '/api/download':
                with app.lock:
                    if app.state['status'] != 'success' or app.output is None:
                        self.reply(409, {'error': 'Build a ROM successfully before downloading.'})
                    else:
                        self.reply(200, app.output.read_bytes(), 'application/octet-stream', 'attachment; filename="Zelda-Genesis.md"')
            elif self.path in ('/', '/app.js', '/style.css'):
                name = {'/': 'index.html', '/app.js': 'app.js', '/style.css': 'style.css'}[self.path]
                content_type = {'/': 'text/html; charset=utf-8', '/app.js': 'text/javascript; charset=utf-8', '/style.css': 'text/css; charset=utf-8'}[self.path]
                self.reply(200, (ASSETS / name).read_bytes(), content_type)
            else:
                self.reply(404, {'error': 'Not found.'})

        def do_POST(self):
            if not self.authorized():
                return
            try:
                size = int(self.headers.get('Content-Length', '-1'))
                if size < 0:
                    self.reply(411, {'error': 'Content length is required.'})
                    return
                if size > MAX_BODY:
                    self.reply(413, {'error': 'This file is too large. Choose a file smaller than 16 MB.'})
                    return
                if self.headers.get('Content-Type', '').split(';')[0] != 'application/json':
                    raise ValueError('Send a JSON request.')
                self.connection.settimeout(15)
                payload = json.loads(self.rfile.read(size))
                if not isinstance(payload, dict):
                    raise ValueError('Invalid request.')
                if self.path == '/api/input':
                    app.upload(payload)
                    self.reply(200, {'ok': True})
                elif self.path == '/api/build':
                    app.start()
                    self.reply(202, {'ok': True})
                elif self.path == '/api/cancel':
                    app.cancel()
                    self.reply(200, {'ok': True})
                elif self.path == '/api/quit':
                    app.cancel()
                    self.reply(200, {'ok': True})
                    threading.Thread(target=self.server.shutdown, daemon=True).start()
                else:
                    self.reply(404, {'error': 'Not found.'})
            except RuntimeError as exc:
                self.reply(409, {'error': str(exc)})
            except (ValueError, TypeError, OSError) as exc:
                self.reply(400, {'error': str(exc)[:1000]})

    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    return server


def main(root=None, prepare=None):
    app = BuilderApplication(root or ROOT, prepare=prepare)
    server = create_server(app)
    url = f'http://127.0.0.1:{server.server_address[1]}/#{app.token}'
    print('Zelda Genesis Builder is running locally. Your ROM stays on this computer.')
    print('Open this link if your browser does not open automatically:\n' + url)
    print('Keep this window open while building. Press Ctrl+C to quit.')
    webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        app.close()
        server.server_close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
