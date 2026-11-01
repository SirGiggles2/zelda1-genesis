// Builder worker (classic). At pack time wasi_mini.js, nesemu.js, uw_dump.js,
// render.js and studio_host.js are inlined above this file (exports stripped).
// The same blob URL also serves as the parallel dungeon-dump helper ("uw" jobs).
// T-419 Task 13.
"use strict";
const PYODIDE_URL = "__PYODIDE_URL__";
let xgmModule = null;
const uwResults = new Map();

self.zeldaHost = {
  xgmtool(vgm) { return xgmtool(xgmModule, vgm); },
  studioRender(json) { return studioRender(json); },
  uwDump(rom, romId, sha, manifestsJson, clean) {
    const r = uwResults.get(`${romId}:${sha}:${clean}`);
    if (!r) throw new Error(`dungeon dump was not prepared for ${romId}`);
    return r;
  },
};

const post = (m, transfer) => self.postMessage(m, transfer || []);

async function uwParallel(rom, romId, sha, manifests) {
  const n = Math.max(1, Math.min(self.navigator.hardwareConcurrency || 4, 8));
  try {
    const parts = await Promise.all(splitJobs(manifests, n).map(jobs => new Promise((resolve, reject) => {
      const w = new Worker(self.location.href);
      w.onmessage = e => { w.terminate(); e.data.error ? reject(new Error(e.data.error)) : resolve(e.data.files); };
      w.onerror = e => { w.terminate(); reject(new Error(e.message || "helper failed")); };
      w.postMessage({ type: "uw", rom, romId, sha, manifests, jobs });
    })));
    return mergeFiles(romId, parts);
  } catch (e) {
    return dumpJobs(rom, romId, sha, manifests, ALL_JOBS, { clean: true });  // no nested workers here
  }
}

function b64bytes(s) {
  const bin = atob(s), out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

async function build(msg) {
  const t0 = performance.now();
  const step = (i, n, label) => post({ type: "progress", i, n, label });
  step(0, 1, "Loading Python");
  try {
    importScripts(PYODIDE_URL + "pyodide.js");
  } catch (e) {
    post({ type: "error", kind: "offline", message: String(e) });
    return;
  }
  let py;
  try {
    py = await loadPyodide({ indexURL: PYODIDE_URL });
    await py.loadPackage(["numpy"]);
  } catch (e) {
    post({ type: "error", kind: "offline", message: String(e) });
    return;
  }
  py.setStdout({ batched: s => post({ type: "log", text: s }) });
  py.setStderr({ batched: s => post({ type: "log", text: s }) });
  xgmModule = await WebAssembly.compile(msg.xgmtool);
  for (const [rel, b64] of Object.entries(msg.bundle)) {
    const path = "/work/" + rel;
    py.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
    py.FS.writeFile(path, b64bytes(b64));
  }
  py.FS.mkdirTree("/work/input");
  py.globals.set("dropped", msg.files);
  py.globals.set("known_json", JSON.stringify(msg.recipe.inputs));
  try {
    py.runPython(`
import hashlib, json, sys
sys.path[:0] = ["/work", "/work/site", "/work/tools/converter", "/work/tools/release/builder_page"]
from inputs import prepare
files = [(f.name, bytes(f.bytes.to_py())) for f in dropped]
rom, redux = prepare(files, json.loads(known_json))
open("/work/input/rom.nes", "wb").write(rom)
open("/work/input/redux.nes", "wb").write(redux)
shas = [hashlib.sha256(rom).hexdigest(), hashlib.sha256(redux).hexdigest()]
`);
  } catch (e) {
    const text = String(e);
    const m = text.match(/InputError: (.*)/);
    post({ type: "error", kind: m ? "input" : "internal", message: m ? m[1].trim() : text });
    return;
  }
  const shas = py.globals.get("shas").toJs();
  const manifests = {};
  for (let l = 1; l <= 9; l++) for (const q of [1, 2]) {
    manifests[`${l}_${q}`] = JSON.parse(py.FS.readFile(`/work/engine/data/uw_level${l}_quest${q}_rooms.json`, { encoding: "utf8" })).rooms;
  }
  step(0, 1, "Reading the dungeons");
  const roms = [["orig", py.FS.readFile("/work/input/rom.nes"), shas[0]], ["redux", py.FS.readFile("/work/input/redux.nes"), shas[1]]];
  const dumps = await Promise.all(roms.map(([id, bytes, sha]) => uwParallel(bytes, id, sha, manifests)));
  roms.forEach(([id, , sha], k) => uwResults.set(`${id}:${sha}:true`, dumps[k]));
  py.globals.set("progress", (i, n, label) => step(i, n, label));
  py.globals.set("recipe_json", JSON.stringify(msg.recipe));
  py.globals.set("base_js", msg.base);
  try {
    py.runPython(`
import json
from pathlib import Path
from tools.nesdata import assemble
import splice
recipe = json.loads(recipe_json)
nesdata = assemble.assemble(Path("/work"), Path("/work/input/rom.nes"), Path("/work/input/redux.nes"),
                            recipe["nesdata"]["offset"], progress)
result = splice.finish(bytes(base_js.to_py()), nesdata, recipe)
`);
  } catch (e) {
    const text = String(e);
    const m = text.match(/MismatchError: built ROM sha256 (\S+) != release (\S+)/);
    post({ type: "error", kind: m ? "mismatch" : "internal", message: text, got: m && m[1], want: m && m[2] });
    return;
  }
  const rom = py.globals.get("result").toJs();
  post({ type: "done", rom, seconds: (performance.now() - t0) / 1000 }, [rom.buffer]);
}

self.onmessage = async e => {
  const msg = e.data;
  if (msg.type === "uw") {
    try {
      post({ files: dumpJobs(msg.rom, msg.romId, msg.sha, msg.manifests, msg.jobs, { clean: true }) });
    } catch (err) {
      post({ error: String(err) });
    }
  } else if (msg.type === "build") {
    try {
      await build(msg);
    } catch (err) {
      post({ type: "error", kind: "internal", message: String(err && err.stack || err) });
    }
  }
};
