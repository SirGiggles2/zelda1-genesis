"""Loopback builder contracts; real HTTP and child processes, no ROM required."""
import base64
import hashlib
import http.client
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import threading
import time
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TEST_TMP = HERE.parents[1] / 'build' / 't413-web-tests'
TEST_TMP.mkdir(parents=True, exist_ok=True)


def temporary_directory():
    return tempfile.TemporaryDirectory(dir=TEST_TMP)


class BuilderHTTPTests(unittest.TestCase):
    def setUp(self):
        self.tmp = temporary_directory()
        self.root = Path(self.tmp.name)
        import web_builder
        self.fixture = self.root / 'fixture.py'
        self.fixture.write_text("import pathlib,sys,time\np=pathlib.Path(sys.argv[1])\nprint('@@step 1/2 Extracting game data',flush=True)\ntime.sleep(.15)\nprint('@@step 2/2 Building Genesis ROM',flush=True)\np.write_bytes(b'GENESIS')\nprint('@@done '+str(p)+' sha256=fixture',flush=True)\n")
        self.app = web_builder.BuilderApplication(
            self.root, command_factory=lambda rom, patch, out: [sys.executable, str(self.fixture), str(out)],
            patch_provider=lambda: b'PATCHfixture')
        self.server = web_builder.create_server(self.app)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.server.server_address[1]

    def tearDown(self):
        if hasattr(self, 'server'):
            self.app.close()
            self.server.shutdown()
            self.server.server_close()
            self.thread.join()
        self.tmp.cleanup()

    def request(self, method, path, data=None, headers=None, authenticated=True):
        con = http.client.HTTPConnection('127.0.0.1', self.port, timeout=5)
        h = {'Host': f'127.0.0.1:{self.port}'}
        if authenticated:
            h['X-Builder-Token'] = self.app.token
        body = json.dumps(data) if data is not None else None
        if body is not None:
            h['Content-Type'] = 'application/json'
        h.update(headers or {})
        con.request(method, path, body, h)
        result = con.getresponse()
        answer = result.status, result.read(), dict(result.getheaders())
        con.close()
        return answer

    def upload(self, name='../../outside.nes', contents=b'NES\x1a' + bytes(131084)):
        return self.request('POST', '/api/input', {'kind': 'rom', 'name': name, 'data': base64.b64encode(contents).decode()})

    def wait_job(self):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            state = json.loads(self.request('GET', '/api/status')[1])
            if state['status'] in ('success', 'error', 'cancelled'):
                return state
            time.sleep(.04)
        self.fail('job did not terminate')

    def test_local_page_is_public_but_sensitive_reads_need_token(self):
        self.assertEqual(200, self.request('GET', '/', authenticated=False)[0])
        for path in ['/api/status', '/api/log', '/api/download']:
            self.assertEqual(403, self.request('GET', path, authenticated=False)[0])

    def test_foreign_host_origin_token_and_paths_are_rejected(self):
        self.assertEqual(403, self.request('GET', '/', headers={'Host': 'evil.example'})[0])
        self.assertEqual(403, self.request('GET', '/', headers={'Origin': 'https://evil.example'})[0])
        self.assertEqual(403, self.request('POST', '/api/build', {}, authenticated=False)[0])
        self.assertEqual(403, self.request('POST', '/api/build', {}, headers={'Origin': 'http://localhost:123'})[0])
        self.assertEqual(404, self.request('GET', '/../../CLAUDE.md')[0])
        self.assertEqual(404, self.request('GET', '/api/download?path=../../secret')[0])

    def test_upload_ignores_user_path_and_bounds_body(self):
        self.assertEqual(200, self.upload()[0])
        files = list((self.root / 'build' / 'builder_inputs').rglob('*.nes'))
        self.assertEqual(1, len(files))
        self.assertTrue(files[0].is_relative_to(self.root))
        self.assertNotIn('outside', files[0].name)
        self.assertEqual(413, self.request('POST', '/api/input', {}, {'Content-Length': '30000000'})[0])
        self.assertEqual(400, self.upload(contents=b'wrong')[0])
        self.assertEqual(400, self.request('POST', '/api/input', {'kind': 'rom', 'data': '!!!'})[0])

    def test_success_progress_download_and_single_job(self):
        self.upload()
        self.assertEqual(202, self.request('POST', '/api/build', {})[0])
        self.assertEqual(409, self.request('POST', '/api/build', {})[0])
        state = self.wait_job()
        self.assertEqual('success', state['status'])
        self.assertEqual(100, state['progress'])
        self.assertEqual(b'GENESIS', self.request('GET', '/api/download')[1])
        self.assertIn(b'Extracting game data', self.request('GET', '/api/log')[1])

    def test_failure_and_exit_zero_without_artifact_never_offer_download(self):
        self.upload()
        for code in ["print('compile failed');raise SystemExit(3)", "print('@@done bogus')"]:
            self.fixture.write_text(code)
            self.request('POST', '/api/build', {})
            state = self.wait_job()
            self.assertEqual('error', state['status'])
            self.assertTrue(state['error'])
            self.assertEqual(409, self.request('GET', '/api/download')[0])

    def test_cancel_stops_child_without_publishing_partial_artifact(self):
        self.fixture.write_text("import time\ntime.sleep(30)")
        self.upload()
        self.request('POST', '/api/build', {})
        time.sleep(.1)
        self.assertEqual(200, self.request('POST', '/api/cancel', {})[0])
        self.assertEqual('cancelled', self.wait_job()['status'])
        self.assertEqual(409, self.request('GET', '/api/download')[0])

    def test_quit_requires_token_and_stops_local_server(self):
        self.assertEqual(403, self.request('POST', '/api/quit', {}, authenticated=False)[0])
        self.assertEqual(200, self.request('POST', '/api/quit', {})[0])
        self.thread.join(timeout=3)
        self.assertFalse(self.thread.is_alive())


class PatchAndLaunchTests(unittest.TestCase):
    def test_freeze_support_runs_before_child_script_dispatch(self):
        with temporary_directory() as d:
            path = Path(d)
            shutil.copyfile(HERE / 'launch.py', path / 'launch.py')
            (path / 'multiprocessing.py').write_text("def freeze_support():\n print('freeze support ready')\n")
            (path / 'child.py').write_text("print('child dispatched')\n")
            run = subprocess.run([sys.executable, str(path / 'launch.py'), str(path / 'child.py')], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stderr)
            self.assertEqual(['freeze support ready', 'child dispatched'], run.stdout.splitlines())

    @unittest.skipUnless(os.name == 'nt', 'Windows console API')
    def test_frozen_ui_hides_console_before_server_without_hiding_child_scripts(self):
        with temporary_directory() as d:
            path = Path(d)
            shutil.copyfile(HERE / 'launch.py', path / 'launch.py')
            (path / 'package_workspace.py').write_text("from pathlib import Path\ndef prepare_workspace():\n return Path(__file__).parent\n")
            (path / 'web_builder.py').write_text("def main(root):\n print('server ready')\n return 0\n")
            (path / 'ctypes.py').write_text("class Kernel:\n def GetConsoleWindow(self): return 123\nclass User:\n def ShowWindow(self,handle,mode): print('hide console',handle,mode)\nclass DLL:\n kernel32=Kernel()\n user32=User()\nwindll=DLL()\n")
            (path / 'runner.py').write_text("import sys,runpy\nsys.frozen=True\nsys.argv=[__file__.replace('runner.py','launch.py')]+sys.argv[1:]\nrunpy.run_path(sys.argv[0],run_name='__main__')\n")
            run = subprocess.run([sys.executable, str(path / 'runner.py')], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stderr)
            self.assertEqual(['hide console 123 0', 'server ready'], run.stdout.splitlines())
            (path / 'child.py').write_text("print('child stdout visible')\n")
            run = subprocess.run([sys.executable, str(path / 'runner.py'), str(path / 'child.py')], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stderr)
            self.assertEqual(['child stdout visible'], run.stdout.splitlines())

    @unittest.skipUnless(os.name == 'nt', 'Windows launcher')
    def test_windows_launcher_finds_python_and_runs_from_path_with_spaces(self):
        with temporary_directory() as d:
            path = Path(d) / 'source with spaces'
            scripts = path / 'tools' / 'converter'
            scripts.mkdir(parents=True)
            (scripts / 'launch.py').write_text("print('source launcher ready')")
            shutil.copyfile(HERE.parents[1] / 'Converter.bat', path / 'Converter.bat')
            run = subprocess.run(['cmd', '/c', str(path / 'Converter.bat')], input='', capture_output=True, text=True, timeout=15)
            self.assertEqual(0, run.returncode, run.stdout + run.stderr)
            self.assertIn('source launcher ready', run.stdout)

    def test_self_test_checks_bundled_imports_and_workspace_without_browser(self):
        with temporary_directory() as d:
            path = Path(d)
            shutil.copyfile(HERE / 'launch.py', path / 'launch.py')
            (path / 'package_workspace.py').write_text("from pathlib import Path\ndef prepare_workspace():\n return Path(__file__).parent\n")
            for name in ('numpy', 'scipy', 'py65'):
                (path / (name + '.py')).write_text("__version__='fixture'\n")
            run = subprocess.run([sys.executable, str(path / 'launch.py'), '--self-test'], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stderr)
            report = json.loads(run.stdout)
            self.assertEqual('ready', report['status'])
            self.assertEqual(str(path), report['workspace'])
            self.assertEqual(['numpy', 'scipy', 'py65'], list(report['modules']))

    def test_source_entry_prepares_runtime_before_opening_server(self):
        with temporary_directory() as d:
            path = Path(d)
            shutil.copyfile(HERE / 'launch.py', path / 'launch.py')
            (path / 'package_workspace.py').write_text("def prepare_runtime():\n print('runtime ready')\n return None\n")
            (path / 'web_builder.py').write_text("def main():\n print('server ready')\n return 0\n")
            run = subprocess.run([sys.executable, str(path / 'launch.py')], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stderr)
            self.assertEqual(['runtime ready', 'server ready'], run.stdout.splitlines())

    def test_patch_cache_requires_pinned_hash(self):
        import web_builder
        with temporary_directory() as d:
            root = Path(d)
            data = b'PATCHverified'
            cache = root / 'build' / 'builder_cache' / 'Zelda1_Redux.ips'
            cache.parent.mkdir(parents=True)
            cache.write_bytes(b'wrong')
            self.assertEqual(data, web_builder.verified_patch(root, download=lambda url: data, expected_hash=hashlib.sha256(data).hexdigest()))
            self.assertEqual(data, cache.read_bytes())
            with self.assertRaises(ValueError):
                web_builder.verified_patch(root, download=lambda url: b'bad', expected_hash='0' * 64)

    def test_frozen_script_dispatch_preserves_arguments_and_imports(self):
        with temporary_directory() as d:
            path = Path(d)
            (path / 'sibling.py').write_text('VALUE=17')
            (path / 'child.py').write_text("import sibling,sys\nprint(sibling.VALUE,sys.argv[1:])")
            run = subprocess.run([sys.executable, str(HERE / 'launch.py'), str(path / 'child.py'), 'with spaces'], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stderr)
            self.assertIn("17 ['with spaces']", run.stdout)


class WorkspaceLockTests(unittest.TestCase):
    def test_separate_applications_cannot_build_the_same_workspace_concurrently(self):
        import web_builder
        with temporary_directory() as d:
            root = Path(d)
            fixture = root / 'slow_build.py'
            fixture.write_text("import pathlib,sys,time\nr=pathlib.Path(sys.argv[2])\nif sys.argv[3]=='first':\n (r/'started').write_text('ready')\n while not (r/'release').exists(): time.sleep(.02)\nelse:\n (r/'second-output').write_text('built')\npathlib.Path(sys.argv[1]).write_bytes(b'GENESIS')\n")
            first = web_builder.BuilderApplication(root, command_factory=lambda rom, patch, out: [sys.executable, str(fixture), str(out), str(root), 'first'], patch_provider=lambda: b'PATCHfirst')
            second = web_builder.BuilderApplication(root, command_factory=lambda rom, patch, out: [sys.executable, str(fixture), str(out), str(root), 'second'], patch_provider=lambda: b'PATCHsecond', prepare=lambda *_: (root / 'second-prepared').write_text('prepared'))
            payload = {'kind': 'rom', 'data': base64.b64encode(b'NES\x1a' + bytes(131084)).decode()}
            first.upload(payload)
            second.upload(payload)
            try:
                first.start()
                deadline = time.monotonic() + 5
                while not (root / 'started').exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertTrue((root / 'started').exists())
                second.start()
                second.worker.join(timeout=5)
                state = second.snapshot()
                self.assertEqual('error', state['status'])
                self.assertIn('Another builder', state['error'])
                self.assertFalse((root / 'second-prepared').exists())
                self.assertFalse((root / 'second-output').exists())
                self.assertEqual('running', first.snapshot()['status'])
                (root / 'release').write_text('go')
                first.worker.join(timeout=5)
                self.assertEqual('success', first.snapshot()['status'])
                # The persistent lock file is safe to reuse after the holder exits.
                second.start()
                second.worker.join(timeout=5)
                self.assertEqual('success', second.snapshot()['status'])
            finally:
                (root / 'release').write_text('go')
                first.close()
                second.close()


if __name__ == '__main__':
    unittest.main()
