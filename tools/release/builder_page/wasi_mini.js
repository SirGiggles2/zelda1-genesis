// Minimal WASI (snapshot_preview1) host for one-shot command-line wasm tools
// such as SGDK xgmtool. Files live in memory: a Map of relative path ->
// Uint8Array, exposed to the program as the preopened directory "/".
// Works unchanged in Node (tests) and in the builder page's worker.
// T-419. Covers exactly the imports xgmtool.wasm needs.

const ESUCCESS = 0, EBADF = 8, ENOENT = 44, EINVAL = 28;
const PREOPEN_FD = 3;

class Exit {
  constructor(code) { this.code = code; }
}

function clean(path) {
  return path.replace(/^(\.\/|\/)+/, "");
}

export function runWasi(module, args, files) {
  const fs = new Map(Object.entries(files));
  const fds = new Map();       // fd -> {path, data, pos}
  let nextFd = PREOPEN_FD + 1;
  let stdout = "", stderr = "";
  let memory = null;
  const enc = new TextEncoder(), dec = new TextDecoder();
  const view = () => new DataView(memory.buffer);
  const bytes = () => new Uint8Array(memory.buffer);
  const argBytes = args.map(a => enc.encode(a + "\0"));
  const readStr = (ptr, len) => dec.decode(bytes().slice(ptr, ptr + len));

  function iovecs(ptr, count) {
    const v = view(), out = [];
    for (let i = 0; i < count; i++) {
      out.push([v.getUint32(ptr + i * 8, true), v.getUint32(ptr + i * 8 + 4, true)]);
    }
    return out;
  }

  const wasi = {
    args_sizes_get(argcPtr, sizePtr) {
      view().setUint32(argcPtr, argBytes.length, true);
      view().setUint32(sizePtr, argBytes.reduce((n, b) => n + b.length, 0), true);
      return ESUCCESS;
    },
    args_get(argvPtr, bufPtr) {
      let p = bufPtr;
      argBytes.forEach((b, i) => {
        view().setUint32(argvPtr + i * 4, p, true);
        bytes().set(b, p);
        p += b.length;
      });
      return ESUCCESS;
    },
    fd_prestat_get(fd, ptr) {
      if (fd !== PREOPEN_FD) return EBADF;
      view().setUint8(ptr, 0);
      view().setUint32(ptr + 4, 1, true);
      return ESUCCESS;
    },
    fd_prestat_dir_name(fd, ptr, len) {
      if (fd !== PREOPEN_FD) return EBADF;
      bytes().set(enc.encode("/").subarray(0, len), ptr);
      return ESUCCESS;
    },
    fd_fdstat_get(fd, ptr) {
      const type = fd === PREOPEN_FD ? 3 : fd <= 2 ? 2 : fds.has(fd) ? 4 : -1;
      if (type < 0) return EBADF;
      const v = view();
      v.setUint8(ptr, type);
      v.setUint16(ptr + 2, 0, true);
      v.setBigUint64(ptr + 8, 0xFFFFFFFFFFFFFFFFn, true);
      v.setBigUint64(ptr + 16, 0xFFFFFFFFFFFFFFFFn, true);
      return ESUCCESS;
    },
    fd_fdstat_set_flags() { return ESUCCESS; },
    path_open(dirFd, _dirFlags, pathPtr, pathLen, oflags, _rb, _ri, _fdflags, fdPtr) {
      if (dirFd !== PREOPEN_FD) return EBADF;
      const path = clean(readStr(pathPtr, pathLen));
      const create = oflags & 1, trunc = oflags & 8;
      if (!fs.has(path)) {
        if (!create) return ENOENT;
        fs.set(path, new Uint8Array(0));
      }
      const fd = nextFd++;
      fds.set(fd, { path, data: trunc ? new Uint8Array(0) : fs.get(path), pos: 0 });
      view().setUint32(fdPtr, fd, true);
      return ESUCCESS;
    },
    fd_read(fd, iovPtr, iovCount, nreadPtr) {
      let n = 0;
      const f = fds.get(fd);
      if (fd !== 0 && !f) return EBADF;
      for (const [buf, len] of iovecs(iovPtr, iovCount)) {
        if (!f) break;
        const chunk = f.data.subarray(f.pos, f.pos + len);
        bytes().set(chunk, buf);
        f.pos += chunk.length;
        n += chunk.length;
        if (chunk.length < len) break;
      }
      view().setUint32(nreadPtr, n, true);
      return ESUCCESS;
    },
    fd_write(fd, iovPtr, iovCount, nwrittenPtr) {
      let n = 0;
      for (const [buf, len] of iovecs(iovPtr, iovCount)) {
        const chunk = bytes().slice(buf, buf + len);
        if (fd === 1) stdout += dec.decode(chunk);
        else if (fd === 2) stderr += dec.decode(chunk);
        else {
          const f = fds.get(fd);
          if (!f) return EBADF;
          if (f.pos + len > f.data.length) {
            const grown = new Uint8Array(Math.max(f.pos + len, f.data.length * 2));
            grown.set(f.data);
            f.data = grown;
            f.size = f.size ?? 0;
          }
          f.data.set(chunk, f.pos);
          f.pos += len;
          f.size = Math.max(f.size ?? 0, f.pos);
        }
        n += len;
      }
      view().setUint32(nwrittenPtr, n, true);
      return ESUCCESS;
    },
    fd_seek(fd, offset, whence, newOffsetPtr) {
      const f = fds.get(fd);
      if (!f) return EBADF;
      const size = f.size ?? f.data.length;
      const base = whence === 0 ? 0 : whence === 1 ? f.pos : whence === 2 ? size : -1;
      if (base < 0) return EINVAL;
      const pos = base + Number(offset);
      if (pos < 0) return EINVAL;
      f.pos = pos;
      view().setBigUint64(newOffsetPtr, BigInt(pos), true);
      return ESUCCESS;
    },
    fd_close(fd) {
      const f = fds.get(fd);
      if (!f) return fd <= 2 ? ESUCCESS : EBADF;
      fs.set(f.path, f.size === undefined ? f.data : f.data.slice(0, f.size));
      fds.delete(fd);
      return ESUCCESS;
    },
    path_unlink_file(dirFd, pathPtr, pathLen) {
      if (dirFd !== PREOPEN_FD) return EBADF;
      return fs.delete(clean(readStr(pathPtr, pathLen))) ? ESUCCESS : ENOENT;
    },
    path_remove_directory() { return ENOENT; },
    proc_exit(code) { throw new Exit(code); },
  };

  const instance = new WebAssembly.Instance(module, { wasi_snapshot_preview1: wasi });
  memory = instance.exports.memory;
  let code = 0;
  try {
    instance.exports._start();
  } catch (e) {
    if (!(e instanceof Exit)) throw e;
    code = e.code;
  }
  for (const fd of [...fds.keys()]) wasi.fd_close(fd);
  return { code, stdout, stderr, files: Object.fromEntries(fs) };
}

// Run SGDK xgmtool on one VGM exactly as tools/converter/local_music.py does natively.
export function xgmtool(module, vgm) {
  const r = runWasi(module, ["xgmtool", "/in.vgm", "/out.bin", "-n", "-s"], { "in.vgm": vgm });
  if (r.code !== 0 || !r.files["out.bin"]) {
    throw new Error(`xgmtool failed (exit ${r.code}): ${(r.stdout + r.stderr).slice(-400)}`);
  }
  return r.files["out.bin"];
}
