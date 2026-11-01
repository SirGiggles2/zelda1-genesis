// Zelda Genesis Builder page (main thread). Data blocks are embedded by
// tools/release/make_builder.py pack(). T-419 Task 13.
"use strict";
(function () {
  const $ = id => document.getElementById(id);
  const text = id => $(id).textContent;
  const MSG = {
    offline: "Connect to the internet once so the builder can download its Python runtime.",
    mismatch: "This build did not match the release. Please report it.",
    internal: "Something went wrong while building. Please report it.",
  };
  const selftest = location.hash === "#selftest";
  const slots = { rom: null, patch: null };  // one file per drop box
  let queued = null, running = false;

  function b64bytes(s) {
    const bin = atob(s.trim()), out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  // Build details: keep every line, show the last 300, redraw at most 4 times a second.
  const logLines = [];
  let logTimer = null;
  function addLog(text) {
    logLines.push(text);
    if (logTimer) return;
    logTimer = setTimeout(() => {
      logTimer = null;
      $("log").textContent = logLines.slice(-300).join("\n");
    }, 250);
  }

  function show(state, message, extra) {
    $("status").dataset.state = state;
    $("status").textContent = message;
    $("details").textContent = extra || "";
  }

  const files = () => [slots.rom, slots.patch].filter(Boolean);

  function listFiles() {
    for (const slot of ["rom", "patch"]) {
      const f = slots[slot];
      $("zone-" + slot).classList.toggle("ready", !!f);
      $("picked-" + slot).textContent = f ? "✓ " + f.name : "Drop it here, or click to choose";
    }
    $("build").disabled = running || !(slots.rom && slots.patch);
  }

  async function take(slot, list) {
    const f = [...list][0];
    if (!f) return;
    const picked = { name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) };
    if (running) { queued = [slot, picked]; return; }  // applied when the current build ends
    slots[slot] = picked;
    show("idle", slots.rom && slots.patch ? "Ready. Click Build." : "");
    listFiles();
  }

  function finish() {
    running = false;
    if (queued) { slots[queued[0]] = queued[1]; queued = null; }
    listFiles();
  }

  function save(rom) {
    const url = URL.createObjectURL(new Blob([rom], { type: "application/octet-stream" }));
    const a = $("save");
    a.href = url;
    a.download = "Zelda Genesis.md";
    a.hidden = false;
    if (!selftest) a.click();  // headless self-test: a save dialog would block the page
  }

  function build() {
    if (running || !(slots.rom && slots.patch)) return;
    running = true;
    listFiles();
    $("save").hidden = true;
    show("busy", "Starting...");
    const started = performance.now();
    const recipe = JSON.parse(text("recipe"));
    const worker = new Worker(URL.createObjectURL(new Blob([text("worker-src")], { type: "text/javascript" })));
    worker.onmessage = e => {
      const m = e.data;
      if (m.type === "progress") {
        show("busy", m.n > 1 ? `Step ${m.i} of ${m.n}: ${m.label}` : m.label);
      } else if (m.type === "log") {
        addLog(m.text);
      } else if (m.type === "done") {
        worker.terminate();
        show("done", "Done. Your ROM is saving as Zelda Genesis.md", `${(m.seconds).toFixed(0)} s`);
        if (selftest) {
          const seconds = (performance.now() - started) / 1000;
          crypto.subtle.digest("SHA-256", m.rom).then(d => {
            const sha256 = [...new Uint8Array(d)].map(b => b.toString(16).padStart(2, "0")).join("");
            window.__zeldaResult = { ok: true, sha256, size: m.rom.length, seconds };
          });
        }
        save(m.rom);
        finish();
      } else if (m.type === "error") {
        worker.terminate();
        const sentence = m.kind === "input" ? m.message : MSG[m.kind] || MSG.internal;
        const extra = m.kind === "mismatch" ? `got ${m.got}\nwant ${m.want}` : m.kind === "input" ? "" : m.message;
        show("error", sentence, extra);
        if (selftest) window.__zeldaResult = { ok: false, kind: m.kind, message: sentence, detail: m.message };
        finish();
      }
    };
    worker.onerror = e => {
      show("error", MSG.internal, e.message);
      if (selftest) window.__zeldaResult = { ok: false, kind: "internal", message: e.message };
      worker.terminate();
      finish();
    };
    const base = b64bytes(text("base"));
    const xgm = b64bytes(text("xgmtool"));
    worker.postMessage({ type: "build", files: files(), recipe, bundle: JSON.parse(text("bundle")), base, xgmtool: xgm },
                       [base.buffer, xgm.buffer]);
  }

  for (const slot of ["rom", "patch"]) {
    const zone = $("zone-" + slot);
    $("pick-" + slot).addEventListener("change", e => take(slot, e.target.files));
    zone.addEventListener("dragover", e => { e.preventDefault(); zone.classList.add("over"); });
    zone.addEventListener("dragleave", () => zone.classList.remove("over"));
    zone.addEventListener("drop", e => { e.preventDefault(); zone.classList.remove("over"); take(slot, e.dataTransfer.files); });
  }
  // A file dropped anywhere else on the page must not make the browser navigate away.
  window.addEventListener("dragover", e => e.preventDefault());
  window.addEventListener("drop", e => e.preventDefault());
  $("build").addEventListener("click", build);
  $("version").textContent = "Release " + JSON.parse(text("recipe")).commit;
  listFiles();
})();
