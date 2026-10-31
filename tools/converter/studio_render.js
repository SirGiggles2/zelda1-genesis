// Headless use of the same deterministic renderer as the local Music Studio.
// stdin holds transient ROM-derived voices. Only mix/instrument settings ship.
"use strict";
const fs = require("fs");
const path = require("path");
const {PRESETS, renderSong} = require("../music_studio/web/render.js");

const {songs, settings} = JSON.parse(fs.readFileSync(0, "utf8"));
const folder = process.argv[2];
const instruments = Object.assign(Object.fromEntries(PRESETS.map(p => [p.name, p])), settings.instruments);
fs.mkdirSync(folder, {recursive: true});
for (const song of songs) {
  if (!/^[a-z0-9_]+$/.test(song.id)) throw new Error("Invalid generated song id");
  const mix = settings.mixes[song.mix_id || song.id];
  if (!mix) throw new Error("Missing builder mix for " + song.id);
  for (const [name, track] of Object.entries(mix)) {
    if (name !== "drums" && track.on && !instruments[track.instrument]) {
      throw new Error("Missing instrument " + track.instrument);
    }
  }
  fs.writeFileSync(path.join(folder, song.id + ".vgm"), renderSong(song, mix, instruments));
}
