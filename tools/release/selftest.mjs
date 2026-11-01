// node selftest.mjs <browser.exe> <page.html> <out.rom> <timeout_s> [--block-cdn] [--double-click] <input files...>
// Opens the builder page from file:// in a headless Chromium browser (DevTools protocol),
// gives it the input files, clicks Build and waits for window.__zeldaResult (#selftest).
// Prints one JSON line: {ok, kind, message, seconds, sha256, builds}. T-419 Task 14.
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const args = process.argv.slice(2);
const flags = new Set(args.filter(a => a.startsWith("--")));
const [browser, page, outRom, timeoutS, ...inputs] = args.filter(a => !a.startsWith("--"));
const profile = mkdtempSync(join(tmpdir(), "t419-selftest-"));
const port = 9400 + Math.floor(Math.random() * 400);
const browserArgs = ["--headless=new", "--disable-gpu", `--user-data-dir=${profile}`, `--remote-debugging-port=${port}`,
  "--no-first-run", "--no-default-browser-check", "--allow-file-access-from-files"];
if (flags.has("--block-cdn")) browserArgs.push("--host-resolver-rules=MAP cdn.jsdelivr.net 0.0.0.0");
const url = pathToFileURL(resolve(page)).href + "#selftest";
const proc = spawn(browser, [...browserArgs, url], { stdio: "ignore" });
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function target() {
  for (let i = 0; i < 100; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      const t = list.find(x => x.type === "page" && x.url.startsWith("file:"));
      if (t) return t.webSocketDebuggerUrl;
    } catch { /* not up yet */ }
    await sleep(200);
  }
  throw new Error("browser did not expose the page");
}

let ws, nextId = 1;
const pending = new Map();
function send(method, params = {}) {
  const id = nextId++;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}
const evaluate = async (expr, ms = 30000) => {
  const reply = await Promise.race([
    send("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true }),
    sleep(ms).then(() => { throw new Error(`page did not answer within ${ms / 1000} s`); }),
  ]);
  return reply.result.value;
};
const events = [];  // crashes, console errors and uncaught exceptions, reported on failure

const result = { ok: false };
try {
  ws = new WebSocket(await target());
  await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) {
      const p = pending.get(m.id); pending.delete(m.id);
      m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result);
    } else if (m.method === "Inspector.targetCrashed") {
      events.push("PAGE CRASHED");
      process.stderr.write("PAGE CRASHED\n");
    } else if (m.method === "Runtime.exceptionThrown") {
      events.push("exception: " + JSON.stringify(m.params.exceptionDetails).slice(0, 600));
    } else if (m.method === "Runtime.consoleAPICalled" && ["error", "warning"].includes(m.params.type)) {
      events.push(`console.${m.params.type}: ` + JSON.stringify(m.params.args.map(a => a.value || a.description)).slice(0, 600));
    } else if (m.method === "Log.entryAdded") {
      events.push(`log ${m.params.entry.level}: ${m.params.entry.text}`.slice(0, 600));
    }
  };
  await send("Inspector.enable");
  await send("Runtime.enable");
  await send("Log.enable");
  for (let i = 0; i < 300 && (await evaluate("document.readyState")) !== "complete"; i++) await sleep(200);
  for (let attempt = 0; ; attempt++) {  // the 9 MB page can still swap documents right after load
    try {
      const { root } = await send("DOM.getDocument");
      // One file per drop box: first argument is the NES ROM, second the Redux patch.
      for (const [selector, file] of [["#pick-rom", inputs[0]], ["#pick-patch", inputs[1]]]) {
        const { nodeId } = await send("DOM.querySelector", { nodeId: root.nodeId, selector });
        await send("DOM.setFileInputFiles", { nodeId, files: [resolve(file)] });
      }
      break;
    } catch (e) {
      if (attempt >= 5) throw e;
      await sleep(500);
    }
  }
  await evaluate("for (const id of ['pick-rom', 'pick-patch']) document.getElementById(id).dispatchEvent(new Event('change'))");
  for (let i = 0; i < 50 && await evaluate("document.getElementById('build').disabled"); i++) await sleep(200);
  const t0 = Date.now();
  await evaluate("document.getElementById('build').click()");
  if (flags.has("--double-click")) await evaluate("document.getElementById('build').click()");
  let r = null, lastShown = 0;
  while (!r && Date.now() - t0 < Number(timeoutS) * 1000) {
    await sleep(1000);
    if (Date.now() - lastShown > 15000) {  // progress on stderr so a stall is visible
      lastShown = Date.now();
      const status = await evaluate("document.getElementById('status').textContent");
      const log = await evaluate("document.getElementById('log').textContent.slice(-200)");
      process.stderr.write(`[${Math.round((Date.now() - t0) / 1000)} s] ${status} | ${JSON.stringify(log)}\n`);
    }
    r = await evaluate("window.__zeldaResult ? {ok: __zeldaResult.ok, kind: __zeldaResult.kind, message: __zeldaResult.message, detail: __zeldaResult.detail, seconds: __zeldaResult.seconds, sha256: __zeldaResult.sha256, size: __zeldaResult.size} : null");
  }
  if (!r) throw new Error(`no result after ${timeoutS} s; status: ${await evaluate("document.getElementById('status').textContent")}`);
  Object.assign(result, r);
  result.builds = await evaluate("document.querySelectorAll('#status').length");
} catch (e) {
  result.kind = result.kind || "harness";
  result.message = String(e && e.stack || e);
} finally {
  result.events = events.slice(-20);
  try { ws && ws.close(); } catch { /* closing */ }
  proc.kill();
  await sleep(500);
  try { rmSync(profile, { recursive: true, force: true }); } catch { /* browser may hold files briefly */ }
}
process.stdout.write(JSON.stringify(result) + "\n");
