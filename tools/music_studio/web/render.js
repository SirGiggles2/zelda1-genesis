// render.js - converted Genesis song + mix -> VGM bytes (YM2612 + PSG).
// Also: FM instrument extraction from any VGM, and built-in presets.
// No DOM here: runs in the browser and under node (bake step).
"use strict";

const FM_RATE_R = 7670453 / 144;
const SLOT_REG = [0, 8, 4, 12];          // S1..S4 -> register offset

// ---- built-in instruments ------------------------------------------------
// ops listed S1, S2, S3, S4 (connection order).  Hand-made starting points.
function op(o) {
  return Object.assign({ dt: 0, mul: 1, tl: 127, ks: 0, ar: 31, am: 0, d1r: 0, d2r: 0, sl: 0, rr: 15, ssg: 0 }, o);
}
const PRESETS = [
  { name: "Bright Lead", alg: 4, fb: 5, ams: 0, pms: 0, ops: [
    op({ mul: 1, tl: 28, d1r: 6, sl: 2, rr: 8 }), op({ mul: 1, tl: 0, d1r: 3, d2r: 2, sl: 2, rr: 7 }),
    op({ mul: 2, dt: 3, tl: 36, d1r: 8, sl: 3, rr: 8 }), op({ mul: 1, dt: 7, tl: 2, d1r: 3, d2r: 2, sl: 2, rr: 7 })] },
  { name: "Square Wave", alg: 4, fb: 6, ams: 0, pms: 0, ops: [
    op({ mul: 2, tl: 30, d1r: 0, sl: 0, rr: 10 }), op({ mul: 1, tl: 0, d1r: 2, d2r: 1, sl: 1, rr: 8 }),
    op({ mul: 2, tl: 127, rr: 15 }), op({ mul: 1, tl: 127, rr: 15 })] },
  { name: "Brass", alg: 4, fb: 4, ams: 0, pms: 0, ops: [
    op({ mul: 1, tl: 24, ar: 18, d1r: 4, sl: 2, rr: 7 }), op({ mul: 1, tl: 0, ar: 22, d1r: 2, d2r: 1, sl: 1, rr: 7 }),
    op({ mul: 1, dt: 3, tl: 28, ar: 18, d1r: 4, sl: 2, rr: 7 }), op({ mul: 1, dt: 7, tl: 4, ar: 22, d1r: 2, d2r: 1, sl: 1, rr: 7 })] },
  { name: "Electric Piano", alg: 4, fb: 3, ams: 0, pms: 0, ops: [
    op({ mul: 14, tl: 42, d1r: 14, sl: 15, rr: 8 }), op({ mul: 1, tl: 2, d1r: 6, d2r: 4, sl: 4, rr: 6 }),
    op({ mul: 1, tl: 30, d1r: 9, sl: 6, rr: 8 }), op({ mul: 1, dt: 3, tl: 4, d1r: 6, d2r: 4, sl: 4, rr: 6 })] },
  { name: "Flute", alg: 4, fb: 2, ams: 0, pms: 0, ops: [
    op({ mul: 1, tl: 40, ar: 16, d1r: 2, sl: 1, rr: 6 }), op({ mul: 1, tl: 0, ar: 18, d1r: 1, sl: 1, rr: 6 }),
    op({ mul: 2, tl: 52, ar: 14, d1r: 2, sl: 1, rr: 6 }), op({ mul: 1, tl: 10, ar: 18, d1r: 1, sl: 1, rr: 6 })] },
  { name: "Strings", alg: 5, fb: 3, ams: 0, pms: 0, ops: [
    op({ mul: 1, tl: 30, ar: 14, d1r: 2, sl: 1, rr: 5 }), op({ mul: 1, tl: 6, ar: 14, d1r: 1, sl: 1, rr: 5 }),
    op({ mul: 1, dt: 3, tl: 8, ar: 14, d1r: 1, sl: 1, rr: 5 }), op({ mul: 1, dt: 7, tl: 8, ar: 14, d1r: 1, sl: 1, rr: 5 })] },
  { name: "Organ", alg: 7, fb: 0, ams: 0, pms: 0, ops: [
    op({ mul: 1, tl: 4, d1r: 0, sl: 0, rr: 9 }), op({ mul: 2, tl: 12, d1r: 0, sl: 0, rr: 9 }),
    op({ mul: 4, tl: 20, d1r: 0, sl: 0, rr: 9 }), op({ mul: 8, tl: 28, d1r: 0, sl: 0, rr: 9 })] },
  { name: "Slap Bass", alg: 0, fb: 6, ams: 0, pms: 0, ops: [
    op({ mul: 1, tl: 30, d1r: 10, sl: 4, rr: 12 }), op({ mul: 0, tl: 32, d1r: 12, sl: 6, rr: 12 }),
    op({ mul: 1, tl: 22, d1r: 8, sl: 3, rr: 12 }), op({ mul: 1, tl: 0, d1r: 5, d2r: 3, sl: 3, rr: 9 })] },
  { name: "Deep Bass", alg: 3, fb: 4, ams: 0, pms: 0, ops: [
    op({ mul: 0, tl: 28, d1r: 4, sl: 3, rr: 10 }), op({ mul: 1, tl: 34, d1r: 6, sl: 4, rr: 10 }),
    op({ mul: 1, tl: 30, d1r: 5, sl: 3, rr: 10 }), op({ mul: 1, tl: 0, d1r: 2, d2r: 1, sl: 1, rr: 8 })] },
];

const CARRIERS_R = [[3],[3],[3],[3],[1,3],[1,2,3],[1,2,3],[0,1,2,3]];

// ---- default mix ---------------------------------------------------------
const TRACK_DEFS = [
  { id: "melody",  label: "Melody",        src: "melody",  fm: 0 },
  { id: "harmony", label: "Harmony",       src: "harmony", fm: 1 },
  { id: "bass",    label: "Bass",          src: "bass",    fm: 2 },
  { id: "echo1",   label: "Melody Echo",   src: "melody",  fm: 3 },
  { id: "echo2",   label: "Harmony Echo",  src: "harmony", fm: 4 },
  { id: "sparkle", label: "Melody Sparkle", src: "melody", fm: 5 },
  { id: "drums",   label: "Drums",         src: "drums",   psg: true },
];
function defaultMix() {
  const base = { on: true, volume: 80, pan: "C", octave: 0, fine: 0, transpose: 0, delay: 0,
    followNes: true, bright: 0, attack: 0, release: 0, solo: false };
  return {
    melody:  Object.assign({}, base, { instrument: "Bright Lead", volume: 85 }),
    harmony: Object.assign({}, base, { instrument: "Square Wave", volume: 65 }),
    bass:    Object.assign({}, base, { instrument: "Slap Bass", volume: 90, followNes: false }),
    echo1:   Object.assign({}, base, { instrument: "Bright Lead", volume: 35, pan: "L", delay: 10 }),
    echo2:   Object.assign({}, base, { instrument: "Square Wave", volume: 30, pan: "R", delay: 10 }),
    sparkle: Object.assign({}, base, { instrument: "Electric Piano", volume: 30, octave: 1, on: false }),
    drums:   Object.assign({}, base, { instrument: "", volume: 70, decay: 50 }),
  };
}

// ---- helpers -------------------------------------------------------------
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
function dbSteps(ratio, stepDb) {           // attenuation steps for amplitude ratio
  if (ratio <= 0) return 999;
  return Math.round(-20 * Math.log10(ratio) / stepDb);
}
function fnumBlock(hz) {
  for (let b = 0; b < 8; b++) {
    const f = Math.round(hz * Math.pow(2, 21 - b) / FM_RATE_R);
    if (f < 2048) return [Math.max(0, f), b];
  }
  return [2047, 7];
}

// Apply the track's simple knobs (brightness / attack / release) to a patch.
function shapePatch(patch, t) {
  const p = JSON.parse(JSON.stringify(patch));
  const car = CARRIERS_R[p.alg];
  p.ops.forEach((o, s) => {
    const isCar = car.includes(s);
    if (!isCar) o.tl = clamp(o.tl - (t.bright | 0), 0, 127);
    o.ar = clamp(o.ar + (t.attack | 0), 0, 31);
    if (isCar) o.rr = clamp(o.rr + (t.release | 0), 0, 15);
  });
  return p;
}

class VgmWriter {
  constructor() { this.b = []; this.samples = 0; this.loopAt = -1; this.loopSample = 0; }
  fm(port, reg, val) { this.b.push(port ? 0x53 : 0x52, reg & 0xFF, val & 0xFF); }
  psg(v) { this.b.push(0x50, v & 0xFF); }
  frame() { this.b.push(0x62); this.samples += 735; }
  markLoop() { this.loopAt = this.b.length; this.loopSample = this.samples; }
  bytes() {
    const data = this.b.concat([0x66]);
    const out = new Uint8Array(0x40 + data.length);
    const dv = new DataView(out.buffer);
    out.set([0x56, 0x67, 0x6D, 0x20], 0);               // "Vgm "
    dv.setUint32(0x04, out.length - 4, true);
    dv.setUint32(0x08, 0x150, true);
    dv.setUint32(0x0C, 3579545, true);                    // SN76489
    dv.setUint32(0x18, this.samples, true);
    if (this.loopAt >= 0) {
      dv.setUint32(0x1C, 0x40 + this.loopAt - 0x1C, true);
      dv.setUint32(0x20, this.samples - this.loopSample, true);
    }
    dv.setUint32(0x24, 60, true);
    dv.setUint16(0x28, 0x0009, true); out[0x2A] = 16;     // Sega PSG LFSR
    dv.setUint32(0x2C, 7670453, true);                    // YM2612
    dv.setUint32(0x34, 0x40 - 0x34, true);
    out.set(data, 0x40);
    return out;
  }
}

function writePatch(w, ch, p, panBits) {
  const port = ch < 3 ? 0 : 1, c = ch % 3;
  p.ops.forEach((o, s) => {
    const r = SLOT_REG[s] + c;
    w.fm(port, 0x30 + r, ((o.dt & 7) << 4) | (o.mul & 15));
    w.fm(port, 0x40 + r, 127);                            // silent until note-on
    w.fm(port, 0x50 + r, ((o.ks & 3) << 6) | (o.ar & 31));
    w.fm(port, 0x60 + r, ((o.am & 1) << 7) | (o.d1r & 31));
    w.fm(port, 0x70 + r, o.d2r & 31);
    w.fm(port, 0x80 + r, ((o.sl & 15) << 4) | (o.rr & 15));
    w.fm(port, 0x90 + r, o.ssg & 15);
  });
  w.fm(port, 0xB0 + c, ((p.fb & 7) << 3) | (p.alg & 7));
  w.fm(port, 0xB4 + c, panBits | ((p.ams & 3) << 4) | (p.pms & 7));
}

// Render one converted song with a mix -> VGM bytes.
//   song: converter output {frames, loop_frame, voices}
//   mix: {trackId: settings}; instruments: name -> patch
function renderSong(song, mix, instruments, opts) {
  opts = opts || {};
  const w = new VgmWriter();
  const frames = song.frames;
  const anySolo = TRACK_DEFS.some(d => mix[d.id] && mix[d.id].solo);
  const live = (d) => { const t = mix[d.id]; return t && t.on && (!anySolo || t.solo); };

  // chip init: LFO, ch3 normal, DAC off, all keys off, PSG silent
  const usedLfo = TRACK_DEFS.some(d => live(d) && !d.psg && instruments[mix[d.id].instrument] &&
    (instruments[mix[d.id].instrument].ams || instruments[mix[d.id].instrument].pms ||
     instruments[mix[d.id].instrument].ops.some(o => o.am)));
  const lfoVal = TRACK_DEFS.map(d => live(d) && !d.psg && instruments[mix[d.id].instrument] &&
    instruments[mix[d.id].instrument].lfo).find(v => v) || 0x0B;
  w.fm(0, 0x22, usedLfo ? (lfoVal | 8) : 0);
  w.fm(0, 0x27, 0); w.fm(0, 0x2B, 0);
  for (const k of [0, 1, 2, 4, 5, 6]) w.fm(0, 0x28, k);
  for (const v of [0x9F, 0xBF, 0xDF, 0xFF]) w.psg(v);

  // per voice peak NES volume (normalise "follow NES volume")
  const peak = {};
  for (const [k, ev] of Object.entries(song.voices)) {
    peak[k] = Math.max(1, ...ev.filter(e => e[1] === "on" || e[1] === "vol").map(e => e[e[1] === "on" ? 3 : 2]));
  }

  // build per-track frame -> events lookup, with delay
  const tracks = [];
  for (const d of TRACK_DEFS) {
    if (!live(d)) continue;
    const t = mix[d.id];
    const byFrame = new Map();
    const delay = clamp(t.delay | 0, 0, 60);
    for (const e of song.voices[d.src] || []) {
      const f = e[0] + delay;
      if (f >= frames) continue;
      if (!byFrame.has(f)) byFrame.set(f, []);
      byFrame.get(f).push(e);
    }
    if (d.psg) { tracks.push({ d, t, byFrame }); continue; }
    const inst = instruments[t.instrument];
    if (!inst) continue;
    const patch = shapePatch(inst, t);
    const panBits = t.pan === "L" ? 0x80 : t.pan === "R" ? 0x40 : 0xC0;
    writePatch(w, d.fm, patch, panBits);
    tracks.push({ d, t, byFrame, patch, keyed: false, nesVol: 0,
      trackAtt: dbSteps((t.volume | 0) / 100, 0.75), peak: peak[d.src] });
  }

  const drumSt = { level: 15, step: 0 };
  const endTail = song.loop_frame == null ? 45 : 0;
  for (let f = 0; f < frames + endTail; f++) {
    if (song.loop_frame != null && f === song.loop_frame) w.markLoop();
    for (const tr of tracks) {
      const evs = tr.byFrame.get(f);
      if (tr.d.psg) { drumFrame(w, tr, evs, drumSt); continue; }
      if (evs) for (const e of evs) fmEvent(w, tr, e);
    }
    if (f === frames) for (const tr of tracks) if (!tr.d.psg && tr.keyed) keyOff(w, tr);
    w.frame();
  }
  return w.bytes();
}

function keyCode(ch) { return ch < 3 ? ch : ch + 1; }
function keyOff(w, tr) { w.fm(0, 0x28, keyCode(tr.d.fm)); tr.keyed = false; }
function setTL(w, tr) {
  const port = tr.d.fm < 3 ? 0 : 1, c = tr.d.fm % 3;
  const nesAtt = tr.t.followNes ? dbSteps(tr.nesVol / tr.peak, 0.75) : 0;
  const car = CARRIERS_R[tr.patch.alg];
  tr.patch.ops.forEach((o, s) => {
    const tl = car.includes(s) ? clamp(o.tl + tr.trackAtt + nesAtt, 0, 127) : o.tl;
    w.fm(port, 0x40 + SLOT_REG[s] + c, tl);
  });
}
function setFreq(w, tr, midi) {
  const port = tr.d.fm < 3 ? 0 : 1, c = tr.d.fm % 3;
  const m = midi + 12 * (tr.t.octave | 0) + (tr.t.fine | 0) / 100 + (tr.t.transpose || 0);
  const hz = 440 * Math.pow(2, (m - 69) / 12);
  const [fn, bl] = fnumBlock(hz);
  w.fm(port, 0xA4 + c, (bl << 3) | (fn >> 8));
  w.fm(port, 0xA0 + c, fn & 0xFF);
}
function fmEvent(w, tr, e) {
  const kind = e[1];
  if (kind === "on") {
    if (tr.keyed) keyOff(w, tr);
    tr.nesVol = e[3];
    setFreq(w, tr, e[2]); setTL(w, tr);
    w.fm(0, 0x28, 0xF0 | keyCode(tr.d.fm)); tr.keyed = true;
  } else if (kind === "pitch") {
    if (tr.keyed) setFreq(w, tr, e[2]);
  } else if (kind === "vol") {
    tr.nesVol = e[2];
    if (tr.keyed && tr.t.followNes) setTL(w, tr);
  } else if (kind === "off") {
    if (tr.keyed) keyOff(w, tr);
  }
}
// Drums on PSG noise: NES noise period index picks hiss colour, the Decay
// knob sets how long each hit rings.
function drumFrame(w, tr, evs, st) {
  const hit = evs && evs.find(e => e[1] === "hit");
  const base = clamp(Math.round(dbSteps((tr.t.volume | 0) / 100, 2)), 0, 15);
  if (hit) {
    const nesIdx = hit[2], len = hit[4];
    const rate = nesIdx <= 4 ? 0 : nesIdx <= 8 ? 1 : 2;
    w.psg(0xE0 | 0x04 | rate);                 // white noise
    const decay = (tr.t.decay == null ? 50 : tr.t.decay) / 100;
    const frames = Math.max(1, Math.round((len <= 2 ? 3 : 8) * (0.4 + decay * 1.6)));
    st.level = base; st.step = (15 - base) / frames;
    w.psg(0xF0 | Math.round(st.level));
  } else if (st.level < 15) {
    st.level = Math.min(15, st.level + st.step);
    w.psg(0xF0 | Math.round(st.level));
  }
}

// ---- instrument extraction from VGM ---------------------------------------
// Every distinct FM patch keyed on in the file (carrier volume normalised),
// plus how the song used it (note range, note lengths) for auto-labelling.
function extractPatches(bytes, sourceName) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (dv.getUint32(0, true) !== 0x206D6756) throw new Error("not a VGM file");
  const ver = dv.getUint32(8, true);
  let pos = ver >= 0x150 && dv.getUint32(0x34, true) ? 0x34 + dv.getUint32(0x34, true) : 0x40;
  const regs = [new Uint8Array(256), new Uint8Array(256)];
  const seen = new Map();            // key -> patch
  const out = [];
  let dacOn = false, now = 0;
  const sounding = [null, null, null, null, null, null];   // per channel {p, start}
  const keyMask = [0, 0, 0, 0, 0, 0];
  const endNote = (ch) => {
    const n = sounding[ch];
    if (n) { n.p.stats.durs.push((now - n.start) / 44100); sounding[ch] = null; }
  };
  const snap = (ch) => {
    const port = ch < 3 ? 0 : 1, c = ch % 3, R = regs[port];
    const p = { alg: R[0xB0 + c] & 7, fb: (R[0xB0 + c] >> 3) & 7,
      ams: (R[0xB4 + c] >> 4) & 3, pms: R[0xB4 + c] & 7, lfo: regs[0][0x22] & 0x0F, ops: [] };
    for (let s = 0; s < 4; s++) {
      const r = SLOT_REG[s] + c;
      p.ops.push({ dt: (R[0x30 + r] >> 4) & 7, mul: R[0x30 + r] & 15, tl: R[0x40 + r] & 127,
        ks: R[0x50 + r] >> 6, ar: R[0x50 + r] & 31, am: R[0x60 + r] >> 7, d1r: R[0x60 + r] & 31,
        d2r: R[0x70 + r] & 31, sl: R[0x80 + r] >> 4, rr: R[0x80 + r] & 15, ssg: R[0x90 + r] & 15 });
    }
    const car = CARRIERS_R[p.alg];
    const minTl = Math.min(...car.map(s => p.ops[s].tl));
    if (minTl >= 120) return null;               // silent key-on
    car.forEach(s => p.ops[s].tl -= minTl);
    if (!p.ams && !p.pms && !p.ops.some(o => o.am)) delete p.lfo;
    const key = JSON.stringify(p);
    if (seen.has(key)) return seen.get(key);
    p.stats = { midis: [], durs: [] };
    seen.set(key, p);
    out.push(p);
    return p;
  };
  const noteOf = (ch) => {
    const port = ch < 3 ? 0 : 1, c = ch % 3, R = regs[port];
    const fnum = ((R[0xA4 + c] & 7) << 8) | R[0xA0 + c], block = (R[0xA4 + c] >> 3) & 7;
    if (!fnum) return null;
    const hz = fnum * FM_RATE_R * Math.pow(2, block - 1) / 1048576;
    return 69 + 12 * Math.log2(hz / 440);
  };
  while (pos < bytes.length) {
    const c = bytes[pos++];
    if (c === 0x52 || c === 0x53) {
      const port = c - 0x52, r = bytes[pos], v = bytes[pos + 1]; pos += 2;
      regs[port][r] = v;
      if (port === 0 && r === 0x2B) dacOn = !!(v & 0x80);
      if (port === 0 && r === 0x28) {
        let ch = v & 7; if (ch === 3 || ch === 7) continue;
        if (ch >= 4) ch -= 1;
        // A note starts when the key goes from all-off to on, and ends when
        // it goes back to all-off; repeated key-on writes are not new notes.
        const wasOn = keyMask[ch], isOn = v & 0xF0;
        keyMask[ch] = isOn;
        if (!isOn) { endNote(ch); continue; }
        if (wasOn || (ch === 5 && dacOn)) continue;
        const p = snap(ch);
        if (!p) continue;
        const m = noteOf(ch);
        if (m != null) p.stats.midis.push(m);
        sounding[ch] = { p, start: now };
      }
    } else if (c === 0x66) break;
    else if (c === 0x67) { const len = dv.getUint32(pos + 2, true); pos += 6 + len; }
    else if (c === 0x61) { now += dv.getUint16(pos, true); pos += 2; }
    else if (c === 0x62) now += 735;
    else if (c === 0x63) now += 882;
    else if (c >= 0x70 && c <= 0x7F) now += (c & 15) + 1;
    else if (c >= 0x80 && c <= 0x8F) now += c & 15;
    else if (c === 0x50 || c === 0x4F || (c >= 0x30 && c <= 0x3F)) pos += 1;
    else if ((c >= 0x40 && c <= 0x4E) || (c >= 0x51 && c <= 0x5F) || (c >= 0xA0 && c <= 0xBF)) pos += 2;
    else if (c >= 0xC0 && c <= 0xDF) pos += 3;
    else if (c === 0xE0 || c >= 0xE1) pos += 4;
    else if (c === 0x90 || c === 0x91 || c === 0x95) pos += 4;
    else if (c === 0x92) pos += 5;
    else if (c === 0x93) pos += 10;
    else if (c === 0x94) pos += 1;
  }
  const base = sourceName.replace(/\.(vgm|vgz)$/i, "");
  out.forEach((p, i) => {
    const g = guessInstrument(p, p.stats);
    p.kind = g.label; p.why = g.why;
    p.name = `${g.label} · ${base} #${i + 1}`;
    p.source = sourceName;
    delete p.stats;
  });
  return out;
}

// ---- auto-label: a best guess from the patch shape + how the song used it ----
function median(a) { if (!a.length) return null; const b = a.slice().sort((x, y) => x - y); return b[b.length >> 1]; }
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
function noteName(m) { const r = Math.round(m); return NOTE_NAMES[((r % 12) + 12) % 12] + (Math.floor(r / 12) - 1); }

function guessInstrument(p, st) {
  const car = CARRIERS_R[p.alg];
  const mods = [0, 1, 2, 3].filter(s => !car.includes(s));
  const C = car.map(s => p.ops[s]);
  const M = mods.map(s => p.ops[s]);
  const mid = median(st.midis), dur = median(st.durs);
  const pitches = new Set(st.midis.map(m => Math.round(m))).size;
  // How the carriers (what you hear) behave over time
  const fades = C.some(o => (o.d1r >= 4 && o.sl >= 6) || o.d2r >= 6);   // dies away like a pluck
  const holds = C.every(o => (o.sl <= 3 || o.d1r <= 2) && o.d2r <= 4);   // stays on like an organ
  const slowAttack = C.every(o => o.ar < 22);
  // Modulators that reach the sound (lower TL = louder = brighter)
  const loudMods = M.filter(o => o.tl < 40);
  const bright = M.length ? (Math.min(...M.map(o => o.tl)) < 26 || p.fb >= 6) : true;
  const metallic = loudMods.some(o => o.mul >= 7);
  const noisy = p.fb >= 6 && loudMods.some(o => o.mul >= 4);
  const say = (label, why) => ({ label, why: why +
    (mid != null ? `; plays around ${noteName(mid)}` : "") +
    (dur != null ? `, notes ~${dur.toFixed(2)}s` : "") });

  if (mid == null) return say("Instrument", "never played a pitched note");
  // Drums: one or two fixed pitches hit over and over, short, often noisy
  if ((pitches <= 2 && st.midis.length >= 4 && (dur == null || dur < 0.3)) ||
      (noisy && fades && dur != null && dur < 0.15)) {
    if (mid < 45 && !noisy) return say("Kick Drum", "short fixed-pitch low hits");
    if (noisy && mid >= 72) return say("Hi-Hat", "short noisy high hits");
    if (noisy) return say("Snare", "short noisy hits");
    return say("Tom", "short fixed-pitch hits");
  }
  if (mid < 48) {
    if (fades) return say(bright ? "Slap Bass" : "Pluck Bass", "low notes that die away");
    return say(bright ? "Synth Bass" : "Deep Bass", "low notes that hold");
  }
  if (metallic && fades) {
    if (p.alg === 4 && loudMods.some(o => o.mul >= 10)) return say("Electric Piano", "high-ratio tine that fades");
    return say(mid >= 72 ? "Bell" : "Mallet", "metallic tone that fades");
  }
  if (p.alg === 7 && holds && !slowAttack) return say("Organ", "all-carrier tone that holds");
  if (holds && slowAttack && (dur == null || dur >= 0.25))
    return say(car.length >= 3 ? "Strings" : "Pad", "slow swell that holds");
  if (holds) {
    if (metallic) return say("Metal Lead", "bright metallic tone that holds");
    if (bright) return say(slowAttack ? "Brass" : (mid >= 72 ? "High Lead" : "Lead"), "bright tone that holds");
    return say(mid >= 67 ? "Flute" : "Soft Lead", "mellow tone that holds");
  }
  if (fades) {
    if (dur != null && dur < 0.2) return say("Pluck", "short notes that die away");
    if (p.alg <= 3 && p.fb >= 4) return say("Guitar", "plucked tone with grit");
    return say(bright ? "Piano" : "Harp", "notes that die away");
  }
  return say(bright ? "Synth" : "Soft Synth", "mixed envelope");
}

// Short test phrase for auditioning one instrument.
function renderAudition(patch) {
  const song = { frames: 150, loop_frame: null, voices: { melody: [
    [0, "on", 60, 15], [18, "off"], [20, "on", 64, 15], [38, "off"], [40, "on", 67, 15], [58, "off"],
    [60, "on", 72, 15], [120, "off"]] } };
  const mix = { melody: { on: true, volume: 90, pan: "C", octave: 0, fine: 0, delay: 0,
    followNes: false, bright: 0, attack: 0, release: 0, instrument: "_a" } };
  return renderSong(song, mix, { _a: patch });
}

if (typeof module !== "undefined") module.exports = {
  PRESETS, TRACK_DEFS, defaultMix, renderSong, renderAudition, extractPatches, guessInstrument, fnumBlock };
