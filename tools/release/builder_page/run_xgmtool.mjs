// node run_xgmtool.mjs <xgmtool.wasm> <in.vgm> <out.bin>
// Runs xgmtool.wasm through the page's own WASI shim (tests compare with native xgmtool).
import { readFileSync, writeFileSync } from "node:fs";
import { xgmtool } from "./wasi_mini.js";

const [wasmPath, vgmPath, outPath] = process.argv.slice(2);
const module = new WebAssembly.Module(readFileSync(wasmPath));
writeFileSync(outPath, xgmtool(module, new Uint8Array(readFileSync(vgmPath))));
