// Browser twin of tools/converter/studio_render.js: render Music Studio songs to VGM.
// Uses render.js (tools/music_studio/web/render.js, inlined before this file).
// Input: JSON {songs, settings}; output: {song id: Uint8Array VGM}. T-419 Task 13.
function studioRender(payloadJson) {
  const { songs, settings } = JSON.parse(payloadJson);
  const instruments = Object.assign(Object.fromEntries(PRESETS.map(p => [p.name, p])), settings.instruments);
  const out = {};
  for (const song of songs) {
    if (!/^[a-z0-9_]+$/.test(song.id)) throw new Error("Invalid generated song id");
    const mix = settings.mixes[song.mix_id || song.id];
    if (!mix) throw new Error("Missing builder mix for " + song.id);
    for (const [name, track] of Object.entries(mix)) {
      if (name !== "drums" && track.on && !instruments[track.instrument]) {
        throw new Error("Missing instrument " + track.instrument);
      }
    }
    out[song.id] = new Uint8Array(renderSong(song, mix, instruments));
  }
  return out;
}
