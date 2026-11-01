// node run_uw_dump.mjs <rom.nes> <rom_id> <rom_sha256> [--sequential]   (manifests JSON on stdin)
// Prints {filename: JSON text} for tools/nesdata/host.py's native uw_dump().
// Default: clean mode (each room from the post-warp state; matches Mesen 918/918).
// --sequential: zelda_uw_dump.py's order-dependent procedure (emulator oracle test only).
// Jobs run in parallel worker threads (one per core); output is order-independent.
import { readFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
import { dumpJobs, mergeFiles, splitJobs } from "./uw_dump.js";

if (isMainThread) {
  const [romPath, romId, romSha, flag] = process.argv.slice(2);
  const clean = flag !== "--sequential";
  const manifests = JSON.parse(readFileSync(0, "utf8"));
  const rom = new Uint8Array(readFileSync(romPath));
  const groups = splitJobs(manifests, availableParallelism());
  const parts = await Promise.all(groups.map(jobs => new Promise((resolve, reject) => {
    const w = new Worker(new URL(import.meta.url), { workerData: { rom, romId, romSha, manifests, jobs, clean } });
    w.once("message", resolve);
    w.once("error", reject);
  })));
  process.stdout.write(JSON.stringify(mergeFiles(romId, parts)));
} else {
  const { rom, romId, romSha, manifests, jobs, clean } = workerData;
  parentPort.postMessage(dumpJobs(rom, romId, romSha, manifests, jobs, { clean }));
}
