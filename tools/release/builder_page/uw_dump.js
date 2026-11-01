// Port of tools/nesemu/zelda_uw_dump.py on the JS NES (nesemu.js), for the
// builder page and the fast native path (tools/nesdata/uw_fast.py).
// Same procedure, pokes, settle rule and JSON. Differences, all output-neutral:
//  - boots once per ROM and clones the machine per level/quest (boot is
//    deterministic and identical for every job);
//  - "fresh" sibling coverage: no previously written sibling files are consulted
//    (T-419 ruling: the native parallel run made this timing-dependent).
// Oracle: tools/nesdata/test_uw_fast.py compares every file with Python's
// dump_level() run against an empty output folder. T-419 Task 10.
import { Nes } from "./nesemu.js";

const IS_UPDATING_MODE = 0x0011, CUR_LEVEL = 0x0010, GAME_MODE = 0x0012, GAME_SUB = 0x0013;
const CUR_SAVE_SLOT = 0x0016, TARGET_MODE = 0x005B, DOORWAY_DIR = 0x0053;
const TRIGGERED_DOOR_CMD = 0x0054, TRIGGERED_DOOR_DIR = 0x0055, ROOM_TRANS = 0x004C;
const ROOM_ID = 0x00EB, NEXT_ROOM_ID = 0x00EC, CUR_OPENED_DOORS = 0x00EE, CUR_PPU_MASK = 0x00FE;
const OPEN_DOORWAY_MASK = 0x033F, NAME_PROGRESS = 0x0421, FADE_CYCLE = 0x051C;
const PREV_OPENED_DOORS = 0x0521, SPAWN_CYCLE = 0x0524, CUR_EDGE_SPAWN = 0x0525;
const CAVE_SOURCE_ROOM = 0x0526, CELLAR_SRC_ROOM = 0x0527, TARGET_MIRROR = 0x0602;
const LEVEL_INFO_START_ROOM = 0x6BAD, QUEST_NUMBERS = 0x062D, SAVE_ACTIVE = [0x0633, 0x0634, 0x0635];
const MAX_BOOT_FRAMES = 20000, MAX_WARP_FRAMES = 1200, MAX_SETTLE_FRAMES = 1500, STABLE_WINDOW = 12;

class Pad {
  constructor() { this.button = null; this.holdLeft = 0; this.releaseLeft = 0; this.releaseAfter = 0; }
  schedule(button, hold = 1, release = 8) {
    if (this.holdLeft > 0 || this.releaseLeft > 0) return;
    this.button = button; this.holdLeft = hold; this.releaseLeft = 0; this.releaseAfter = release;
  }
  buttons() {
    if (this.holdLeft > 0 && this.button) {
      this.holdLeft -= 1;
      if (this.holdLeft === 0) this.releaseLeft = this.releaseAfter;
      return [this.button];
    }
    if (this.releaseLeft > 0) this.releaseLeft -= 1;
    return [];
  }
}

function advance(nes, buttons = []) { nes.setButtons(...buttons); nes.runFrame(); }

export function bootToOverworld(nes) {
  const u8 = a => nes.bus(a);
  const pad = new Pad();
  let flow = "boot", lastName = u8(NAME_PROGRESS), nameEvents = 0, selectsIgnored = 0, lastSelectSlot = null;
  for (let i = 0; i < MAX_BOOT_FRAMES; i++) {
    const mode = u8(GAME_MODE), slot = u8(CUR_SAVE_SLOT), name = u8(NAME_PROGRESS);
    const active = SAVE_ACTIVE.map(u8);
    if (flow === "boot") {
      if (mode === 0x01) flow = "select_register"; else pad.schedule("Start", 2, 3);
    } else if (flow === "select_register") {
      if (slot === 0x03) flow = "enter_register"; else pad.schedule("Down", 1, 10);
    } else if (flow === "enter_register") {
      if (mode === 0x0E) { flow = "type_name"; lastName = name; }
      else if (mode === 0x01) pad.schedule("Start", 2, 14);
    } else if (flow === "type_name") {
      if (name !== lastName) { nameEvents += 1; lastName = name; }
      if (nameEvents >= 5) flow = "finish_name"; else pad.schedule("A", 1, 10);
    } else if (flow === "finish_name") {
      if (mode !== 0x0E) flow = "wait_gameplay";
      else if (slot !== 0x03 && selectsIgnored < 2) {
        if (pad.holdLeft === 0 && pad.releaseLeft === 0) {
          if (lastSelectSlot === slot) selectsIgnored += 1;
          lastSelectSlot = slot;
        }
        pad.schedule("Select", 1, 10);
      } else pad.schedule("Start", 2, 14);
    } else if (flow === "wait_gameplay") {
      if (mode === 0x01) flow = "start_game";
    } else if (flow === "start_game") {
      if (mode !== 0x01) flow = "wait_gameplay";
      else {
        let target = 0;
        if (active[0] === 0 && active[1] !== 0) target = 1;
        else if (active[0] === 0 && active[1] === 0 && active[2] !== 0) target = 2;
        if (slot !== target) pad.schedule(target > slot ? "Down" : "Up", 1, 10);
        else pad.schedule("Start", 2, 14);
      }
    }
    advance(nes, pad.buttons());
    if (u8(CUR_LEVEL) === 0 && u8(GAME_MODE) === 0x05 && u8(GAME_SUB) === 0 && u8(ROOM_ID) === 0x77 && u8(ROOM_TRANS) === 0) {
      for (let k = 0; k < 30; k++) advance(nes);
      return true;
    }
  }
  return false;
}

function forceQuest(nes, quest) {
  if (quest === 2) nes.poke(QUEST_NUMBERS + nes.bus(CUR_SAVE_SLOT), 1);
}

function warpToLevel(nes, level) {
  const u8 = a => nes.bus(a);
  for (const [a, v] of [[CUR_LEVEL, level], [TARGET_MODE, 2], [TARGET_MIRROR, 2], [GAME_MODE, 0x10], [GAME_SUB, 0]]) nes.poke(a, v);
  nes.setButtons();
  for (let i = 0; i < MAX_WARP_FRAMES; i++) {
    nes.runFrame();
    if (u8(CUR_LEVEL) === level && u8(GAME_MODE) === 0x05 && u8(GAME_SUB) === 0) {
      let stable = 0;
      for (let k = 0; k < 60; k++) {
        nes.runFrame();
        if (u8(CUR_LEVEL) === level && u8(GAME_MODE) === 0x05 && u8(GAME_SUB) === 0) {
          stable += 1;
          if (stable >= 30) return true;
        } else stable = 0;
      }
    }
  }
  return false;
}

function forceRoom(nes, target) {
  for (const a of [ROOM_TRANS, DOORWAY_DIR, TRIGGERED_DOOR_CMD, TRIGGERED_DOOR_DIR, CUR_OPENED_DOORS, OPEN_DOORWAY_MASK,
    PREV_OPENED_DOORS, SPAWN_CYCLE, CUR_EDGE_SPAWN, CAVE_SOURCE_ROOM, CELLAR_SRC_ROOM, FADE_CYCLE]) nes.poke(a, 0);
  for (const [a, v] of [[LEVEL_INFO_START_ROOM, target], [ROOM_ID, target], [NEXT_ROOM_ID, target],
    [GAME_MODE, 0x03], [GAME_SUB, 0x02], [IS_UPDATING_MODE, 0x00]]) nes.poke(a, v);
}

function ntRows(nes) {
  const rows = [];
  for (let r = 0; r < 30; r++) rows.push(Array.from(nes.ciram.subarray(r * 32, (r + 1) * 32)));
  return rows;
}

function sameBytes(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

function settleAndCapture(nes, target, waitForGanon) {
  const u8 = a => nes.bus(a);
  let stable = 0, prev = null;
  nes.setButtons();
  for (let f = 1; f <= MAX_SETTLE_FRAMES; f++) {
    nes.runFrame();
    const mask = u8(CUR_PPU_MASK);
    if (u8(GAME_MODE) === 0x05 && u8(GAME_SUB) === 0 && u8(IS_UPDATING_MODE) === 1
        && (!waitForGanon || u8(0x0445) === 0x02)
        && (mask & 0x1F) >= 0x18 && u8(ROOM_ID) === target && (mask & 0x18) === 0x18) {
      const state = nes.ciram.slice(0, 0x400);
      if (prev !== null && sameBytes(state, prev)) {
        stable += 1;
        if (stable >= STABLE_WINDOW) {
          return [f, ntRows(nes), Array.from(nes.ciram.subarray(0x3C0, 0x400)), Array.from(nes.palram)];
        }
      } else { stable = 0; prev = state; }
    } else { stable = 0; prev = null; }
  }
  return [MAX_SETTLE_FRAMES, null, null, null];
}

export function normalRoomInventory(attrs, info, level) {
  if (attrs.length !== 768 || info.length !== 256) throw new Error("incomplete installed dungeon records");
  const cellars = new Set([...info.slice(0x34, 0x3e)].filter(r => r < 128));
  const rooms = new Set([info[0x2f], info[0x30], info[0x3e]].filter(r => r < 128));
  for (const c of cellars) { rooms.add(attrs[c]); rooms.add(attrs[128 + c]); }
  if (level === 9) {
    for (let r = 0; r < 128; r++) {
      const v = (attrs[256 + r] & 63) | ((attrs[384 + r] & 128) >> 1);
      if (v === 0x47 || v === 0x48) rooms.add(r);
    }
  }
  const seen = new Set();
  const queue = [...rooms].filter(r => r < 128 && !cellars.has(r)).sort((a, b) => a - b);
  while (queue.length) {
    const room = queue.shift();
    if (seen.has(room) || cellars.has(room)) continue;
    seen.add(room);
    const a = attrs[room], b = attrs[128 + room];
    for (const [door, delta] of [[(a >> 5) & 7, -16], [(a >> 2) & 7, 16], [(b >> 2) & 7, 1], [(b >> 5) & 7, -1]]) {
      const dest = room + delta;
      if (door === 1 || dest < 0 || dest >= 128 || (Math.abs(delta) === 1 && Math.floor(room / 16) !== Math.floor(dest / 16))) continue;
      if (!seen.has(dest) && !cellars.has(dest)) queue.push(dest);
    }
  }
  return [...seen].sort((x, y) => x - y);
}

// clean: every room starts from the post-warp state (no door/shutter state carried
// over from the previous capture) — the NES ground-truth definition checked on Mesen.
export function dumpLevel(booted, romId, romSha256, level, quest, manifestRooms, { clean = false } = {}) {
  let nes = booted.clone();
  const out = { system_id: "NES", boot_ok: true, warp_ok: false, rom: romId, rom_sha256: romSha256,
    map_name: "nesemu", level, quest, rooms_requested: 0, results: [] };
  forceQuest(nes, quest);
  if (!warpToLevel(nes, level)) { out.fatal_error = "warp_failed"; return out; }
  out.warp_ok = true;
  const attrs = [], info = [];
  for (let a = 0x687e; a < 0x6b7e; a++) attrs.push(nes.bus(a));
  for (let a = 0x6b7e; a < 0x6c7e; a++) info.push(nes.bus(a));
  const required = normalRoomInventory(attrs, info, level);
  const cellars = new Set(info.slice(0x34, 0x3e).filter(r => r < 128));
  const rooms = manifestRooms.map(x => parseInt(x, 16)).filter(r => !cellars.has(r));
  for (const r of required) if (!rooms.includes(r)) rooms.push(r);
  out.rooms_requested = rooms.length;
  out.required_rooms = required;
  out.installed_level_block = attrs;
  out.installed_level_info = info;
  const ganon = level === 9 && quest === 1 && romId === "orig";
  const warped = clean ? nes.clone() : null;
  for (const rid of rooms) {
    if (clean) nes = warped.clone();
    forceRoom(nes, rid);
    const [frames, nt, attr, pal] = settleAndCapture(nes, rid, ganon && rid === 0x42);
    if (nt === null) {
      out.results.push({ room_id: rid, code: "settle_timeout", settle_frames: frames });
      continue;
    }
    out.results.push({ room_id: rid, cur_level: nes.bus(CUR_LEVEL), game_mode: nes.bus(GAME_MODE),
      game_sub: nes.bus(GAME_SUB), quest, ppu_mask: nes.bus(CUR_PPU_MASK), settle_frames: frames,
      nt, attr, palram: pal });
  }
  out.rooms_captured = out.results.filter(r => "nt" in r).length;
  out.rooms_failed = out.results.length - out.rooms_captured;
  return out;
}

// Python json.dumps(obj) text (default separators ", " and ": ", ensure_ascii).
export function pyJson(v) {
  if (v === null) return "null";
  if (v === true) return "true";
  if (v === false) return "false";
  if (typeof v === "number") return String(v);
  if (typeof v === "string") return JSON.stringify(v).replace(/[\u007f-￿]/g, c => "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0"));
  if (Array.isArray(v)) return "[" + v.map(pyJson).join(", ") + "]";
  return "{" + Object.entries(v).map(([k, x]) => pyJson(k) + ": " + pyJson(x)).join(", ") + "}";
}

export const ALL_JOBS = [];
for (let level = 1; level <= 9; level++) for (const quest of [1, 2]) ALL_JOBS.push([level, quest]);

// Some levels/quests -> {filename: file text}. manifests: {"L_Q": [room hex]}.
export function dumpJobs(rom, romId, romSha256, manifests, jobs = ALL_JOBS, options = {}) {
  const booted = new Nes(rom);
  const files = {};
  const ok = bootToOverworld(booted);
  for (const [level, quest] of jobs) {
    const name = `nes_uw_level${level}_quest${quest}_${romId}.json`;
    const res = ok ? dumpLevel(booted, romId, romSha256, level, quest, manifests[`${level}_${quest}`], options)
      : { system_id: "NES", boot_ok: false, warp_ok: false, rom: romId, rom_sha256: romSha256,
          map_name: "nesemu", level, quest, rooms_requested: 0, results: [], fatal_error: "boot_failed" };
    files[name] = pyJson(res) + "\n";
  }
  return files;
}

export const dumpAll = (rom, romId, romSha256, manifests) => dumpJobs(rom, romId, romSha256, manifests);

// Split the jobs into `workers` groups of similar cost (manifest room count; the
// output does not depend on the split: every job starts from the same booted state).
export function splitJobs(manifests, workers) {
  const cost = ([l, q]) => manifests[`${l}_${q}`].length + (l === 9 ? 40 : 10);
  const groups = Array.from({ length: Math.max(1, Math.min(workers, ALL_JOBS.length)) }, () => ({ cost: 0, jobs: [] }));
  for (const job of [...ALL_JOBS].sort((a, b) => cost(b) - cost(a))) {
    const g = groups.reduce((m, x) => (x.cost < m.cost ? x : m));
    g.jobs.push(job);
    g.cost += cost(job);
  }
  return groups.map(g => g.jobs).filter(j => j.length);
}

// Merge worker results back into the canonical job order.
export function mergeFiles(romId, parts) {
  const all = Object.assign({}, ...parts), out = {};
  for (const [l, q] of ALL_JOBS) {
    const name = `nes_uw_level${l}_quest${q}_${romId}.json`;
    out[name] = all[name];
  }
  return out;
}
