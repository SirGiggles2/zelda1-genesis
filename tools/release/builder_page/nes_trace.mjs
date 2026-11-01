// node nes_trace.mjs <rom.nes> <frames>
// Prints one line per frame: sha256 of the machine state after the frame, with the
// same scripted buttons as tools/nesdata/test_nesemu_js.py (lockstep vs nes.py).
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { Nes } from "./nesemu.js";

export function scriptedButtons(frame) {
  // Start, then a name (A presses), Select/Start to register, Start to play, walk.
  const phase = frame % 97;
  if (frame < 1200) return phase < 2 ? ["Start"] : phase >= 40 && phase < 41 ? ["A"] : phase === 70 ? ["Select"] : phase === 80 ? ["Down"] : [];
  return [["Up"], ["Left"], ["Down"], ["Right"], ["A"], ["B"]][(frame >> 5) % 6];
}

export function digest(nes) {
  const c = nes.cpu, h = createHash("sha256");
  for (const a of [nes.ram, nes.wram, nes.chr, nes.ciram, nes.palram, nes.oam]) h.update(a);
  h.update(`${c.pc},${c.a},${c.x},${c.y},${c.sp},${c.p},${c.processorCycles},${nes.v},${nes.t},${nes.w},${nes.ppuCtrl},${nes.mmc1Control},${nes.mmc1Prg}`);
  return h.digest("hex");
}

if (process.argv[1] && process.argv[1].endsWith("nes_trace.mjs")) {
  const [romPath, frames] = process.argv.slice(2);
  const nes = new Nes(new Uint8Array(readFileSync(romPath)));
  const out = [];
  for (let f = 0; f < Number(frames); f++) {
    nes.setButtons(...scriptedButtons(f));
    nes.runFrame();
    out.push(digest(nes));
  }
  process.stdout.write(out.join("\n") + "\n");
}
