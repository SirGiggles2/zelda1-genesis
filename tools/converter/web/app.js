"use strict";
const $ = (id) => document.getElementById(id);
const token = location.hash.slice(1) || sessionStorage.getItem("builder-token") || "";
// Keep the secret out of subsequent history/referrer URLs. It only lives in this tab.
if (token) {sessionStorage.setItem("builder-token", token); history.replaceState(null, "", location.pathname);}
let ready = false, running = false, uploading = false, polling = false, stopped = false;
async function api(path, data) {
  const response = await fetch(path, {method: data === undefined ? "GET" : "POST", headers: {"X-Builder-Token": token, "Content-Type": "application/json"}, body: data === undefined ? undefined : JSON.stringify(data)});
  if (!response.ok) { const detail = await response.json(); throw new Error(detail.error || "The local builder could not finish this request."); }
  return response;
}
function error(message) { $("error").textContent = message; $("error").hidden = !message; }
function controls() { $("build").disabled = !ready || running || uploading; $("rom").disabled = running || uploading; $("patch").disabled = running || uploading; }
async function upload(file, kind) {
  if (!file || running || uploading) return;
  error("");
  if (file.size > 16 * 1024 * 1024) { error("Choose a file smaller than 16 MB."); return; }
  uploading = true; controls();
  try {
    const data = new Uint8Array(await file.arrayBuffer());
    let binary = "";
    for (let i = 0; i < data.length; i += 32768) binary += String.fromCharCode(...data.subarray(i, i + 32768));
    await api("/api/input", {kind, name: file.name, data: btoa(binary)});
    if (kind === "rom") { ready = true; $("rom-name").textContent = file.name; $("rom-help").textContent = "Ready to build · click to choose a different ROM"; }
    else $("patch-name").textContent = " Selected: " + file.name;
  } catch (e) { error(e.message); } finally { uploading = false; controls(); }
}
$("rom").addEventListener("change", (e) => upload(e.target.files[0], "rom"));
$("patch").addEventListener("change", (e) => upload(e.target.files[0], "patch"));
for (const name of ["dragenter", "dragover"]) $("dropzone").addEventListener(name, (e) => {e.preventDefault(); $("dropzone").classList.add("over");});
for (const name of ["dragleave", "drop"]) $("dropzone").addEventListener(name, (e) => {e.preventDefault(); $("dropzone").classList.remove("over");});
$("dropzone").addEventListener("drop", (e) => upload(e.dataTransfer.files[0], "rom"));
$("build").addEventListener("click", async () => {
  error(""); running = true; controls(); $("success").hidden = true;
  try { await api("/api/build", {}); await refresh(); } catch (e) { running = false; controls(); error(e.message); }
});
$("cancel").addEventListener("click", async () => {try {await api("/api/cancel", {});} catch (e) {error(e.message);}});
$("quit").addEventListener("click", async () => {
  try {
    await api("/api/quit", {}); stopped = true; ready = false; running = false; controls();
    $("quit").disabled = true; $("cancel").hidden = true; $("success").hidden = true;
    error("Builder closed. You can close this tab. Reopen Converter to build again.");
  } catch (e) {error(e.message);}
});
async function download(path, filename) {
  try {const response = await api(path); const url = URL.createObjectURL(await response.blob()); const link = document.createElement("a"); link.href = url; link.download = filename; link.click(); setTimeout(() => URL.revokeObjectURL(url), 60000);} catch (e) {error(e.message);}
}
$("download").addEventListener("click", () => download("/api/download", "Zelda-Genesis.md"));
$("download-log").addEventListener("click", () => download("/api/log", "builder.log"));
async function refresh() {
  if (polling || !token || stopped) return;
  polling = true;
  try {
    const state = await (await api("/api/status")).json();
    running = state.status === "running"; ready = state.rom_ready;
    $("progress-panel").hidden = state.status === "idle";
    $("cancel").hidden = !running;
    $("stage").textContent = state.stage;
    $("percent").textContent = state.progress + "%";
    $("progress").value = state.progress;
    $("success").hidden = state.status !== "success";
    $("progress-note").hidden = !running;
    $("details").hidden = state.status === "idle";
    const logs = state.logs.join("\n");
    if ($("logs").textContent !== logs) {$("logs").textContent = logs; $("logs").scrollTop = $("logs").scrollHeight;}
    if (state.error) error(state.error);
    controls();
  } catch (e) {error("The local builder is unavailable. Keep its launcher open, or reopen the builder.");}
  finally {polling = false;}
}
if (!token) error("Open the builder using Converter or the link in its launcher window.");
refresh();
setInterval(refresh, 750);
