// ITK-SNAP Assistant MVP - web chat + live viewer
const sid = (crypto.randomUUID && crypto.randomUUID()) || ("s" + Date.now());
let axis = "axial";
let currentIndex = null;

const $ = (id) => document.getElementById(id);
const messages = $("messages");

// ---- websocket ----------------------------------------------------------
const proto = location.protocol === "https:" ? "wss" : "ws";
const ws = new WebSocket(`${proto}://${location.host}/ws/${sid}`);
let statusEl = null;

ws.onmessage = (ev) => handleEvent(JSON.parse(ev.data));
ws.onclose = () => addStatus("disconnected", true);

function refreshBadge() {
  fetch("/api/config").then(r => r.json()).then(c => {
    $("backend").textContent = c.label || c.backend;
    $("s_backend").value = c.backend;
    $("s_base").value = c.base_url || "";
    $("s_model").value = c.model || "";
  });
}
refreshBadge();

// ---- event handling -----------------------------------------------------
let streamMsg = null, streamThought = null;

function handleEvent(e) {
  switch (e.type) {
    case "ready": break;
    case "user": addMsg("user", e.text); break;
    case "token": appendStream(e.text); break;         // live assistant tokens
    case "thinking": appendThought(e.text); break;     // live reasoning stream
    case "thought": addMsg("thought", e.text); break;
    case "status": setStatus(e.text); break;
    case "assistant": clearStatus(); finalizeStream(e.text); break;
    case "error": clearStatus(); endStreams(); addMsg("error", e.text); break;
    case "tool_start": clearStatus(); endStreams(); addToolStart(e.name, e.args); break;
    case "tool_result": addToolResult(e.name, e); break;
    case "viewer": onViewerUpdate(e.focus); break;
    case "turn_end": clearStatus(); endStreams(); enableInput(true); break;
  }
  messages.scrollTop = messages.scrollHeight;
}

function appendStream(text) {
  clearStatus();
  if (!streamMsg) { streamMsg = document.createElement("div"); streamMsg.className = "msg assistant"; messages.appendChild(streamMsg); }
  streamMsg.textContent += text;
}
function finalizeStream(text) {
  if (streamMsg) { if (text) streamMsg.textContent = text; streamMsg = null; }
  else if (text) addMsg("assistant", text);
}
function appendThought(text) {
  if (!streamThought) { streamThought = document.createElement("div"); streamThought.className = "msg thought stream"; streamThought.textContent = "reasoning: "; messages.appendChild(streamThought); }
  streamThought.textContent += text;
}
function endStreams() { streamMsg = null; streamThought = null; }

function addMsg(cls, text) {
  const d = document.createElement("div");
  d.className = "msg " + cls;
  d.textContent = text;
  messages.appendChild(d);
}
function setStatus(text) {
  clearStatus();
  statusEl = document.createElement("div");
  statusEl.className = "status";
  statusEl.innerHTML = `<span class="dot"></span><span></span>`;
  statusEl.lastChild.textContent = text;
  messages.appendChild(statusEl);
}
function clearStatus() { if (statusEl) { statusEl.remove(); statusEl = null; } }
function addStatus(text, err) { addMsg(err ? "error" : "thought", text); }

let lastTool = null;
function addToolStart(name, args) {
  const d = document.createElement("div");
  d.className = "tool";
  d.innerHTML = `<div class="name">${name}()</div>
                 <div class="args">${escapeHtml(JSON.stringify(args))}</div>
                 <div class="result running">running…</div>`;
  messages.appendChild(d);
  lastTool = d;
}
function addToolResult(name, e) {
  if (!lastTool) return;
  const r = lastTool.querySelector(".result");
  r.className = "result " + (e.ok ? "ok" : "err");
  r.textContent = e.text || (e.ok ? "done" : "error");
}

// ---- viewer -------------------------------------------------------------
function onViewerUpdate(focus) {
  if (focus && focus.axis) { setAxis(focus.axis); }
  refreshInfo(focus && focus.index != null ? focus.index : null);
}

function refreshInfo(forceIndex) {
  fetch(`/api/session/${sid}/info?axis=${axis}`).then(r => r.json()).then(info => {
    if (!info.has_image) return;
    const slider = $("slider");
    slider.max = info.n_slices - 1;
    slider.disabled = false;
    if (forceIndex != null) currentIndex = forceIndex;
    if (currentIndex == null || currentIndex > info.n_slices - 1)
      currentIndex = Math.floor(info.n_slices / 2);
    slider.value = currentIndex;
    drawSlice();
  });
}

function drawSlice() {
  const img = $("slice");
  img.src = `/api/session/${sid}/slice.png?axis=${axis}&index=${currentIndex}&overlay=1&t=${Date.now()}`;
  img.hidden = false;
  $("placeholder").style.display = "none";
  $("sliceLabel").textContent = `${axis} ${currentIndex}`;
}

function setAxis(a) {
  axis = a;
  document.querySelectorAll("#axes button").forEach(b =>
    b.classList.toggle("on", b.dataset.axis === a));
}

// ---- input --------------------------------------------------------------
function enableInput(on) {
  $("input").disabled = !on; $("send").disabled = !on;
  if (on) $("input").focus();
}
function send(text) {
  if (!text.trim() || ws.readyState !== 1) return;
  enableInput(false);
  ws.send(JSON.stringify({ text }));
}

$("composer").addEventListener("submit", (e) => {
  e.preventDefault();
  const v = $("input").value;
  $("input").value = "";
  send(v);
});
document.querySelectorAll(".chip").forEach(c =>
  c.addEventListener("click", () => send(c.dataset.msg)));
document.querySelectorAll("#axes button").forEach(b =>
  b.addEventListener("click", () => { setAxis(b.dataset.axis); refreshInfo(); }));
$("slider").addEventListener("input", (e) => {
  currentIndex = parseInt(e.target.value, 10);
  drawSlice();
});

function escapeHtml(s) {
  return s.replace(/[&<>"']/g, c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;" }[c]));
}

// ---- LLM endpoint settings (point at your cluster at runtime) ------------
$("gear").addEventListener("click", () => {
  const s = $("settings"); s.hidden = !s.hidden;
});
$("s_apply").addEventListener("click", () => {
  const body = {
    backend: $("s_backend").value,
    base_url: $("s_base").value,
    model: $("s_model").value,
    api_key: $("s_key").value,
  };
  setStatusLine("applying…");
  fetch("/api/llm", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body) })
    .then(r => r.json()).then(st => { setStatusLine("applied: " + st.label, "ok"); refreshBadge(); })
    .catch(e => setStatusLine("error: " + e, "err"));
});
$("s_ping").addEventListener("click", () => {
  setStatusLine("pinging…");
  fetch("/api/llm/ping").then(r => r.json()).then(p => {
    if (p.ok) setStatusLine("reachable" + (p.models && p.models.length ? " — models: " + p.models.join(", ") : ""), "ok");
    else setStatusLine("unreachable: " + p.error, "err");
  });
});
function setStatusLine(text, cls) {
  const el = $("s_status"); el.textContent = text; el.className = cls || "";
}
