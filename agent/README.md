# ITK-SNAP Agent (MVP)

A natural-language **chat + live viewer** that drives medical-image tasks
(load, segment, measure, view) through a tool-calling LLM. This is the "brain"
that will later drop **inside ITK-SNAP** as a Qt dock panel (QWebEngineView +
QWebChannel). Right now it runs standalone so you can test the whole loop today.

```
 web chat UI  ──ws──▶  FastAPI  ──▶  Agent loop  ──▶  Tools (SimpleITK)
   (live viewer) ◀── slice PNG ◀──                     load / segment / volume
```

## What works now
- Chat over a websocket with a **live tool timeline** (see each step happen).
- A **live viewer**: axial/coronal/sagittal slices with segmentation overlay and a scrub slider.
- Tools: `load_demo`, `load_image`, `threshold_segment`, `compute_volume`, `list_labels`, `show_slice`.
- **Backend-agnostic LLM**: `mock` (default, no setup), `ollama`, `anthropic`, `openai`.

## Quick start (Windows / PowerShell)
```powershell
cd D:\itksnap-agent
python -m venv .venv; .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
.\run.ps1                      # starts on http://127.0.0.1:8077
```
Open http://127.0.0.1:8077 and try:
1. `load demo`
2. `segment the bright nodule`
3. `what is its volume?`
4. `show a coronal slice`  (or drag the slider)

The mock LLM makes this fully deterministic with **no keys and no GPU**.

## Point it at your llama.cpp cluster model (live testing)
`llama-server` exposes an OpenAI-compatible `/v1` API, which is exactly what the
agent's `OpenAICompatBackend` speaks (same design as RadAssistant).

**Critical: launch llama-server with `--jinja`** — tool calling only works when
the model's chat template is active. Use a tool-capable *instruct* model
(Qwen2.5-Instruct, Llama-3.1-Instruct, Hermes, functionary); a base model will
not call tools.

```bash
# On the cluster / GPU node:
llama-server -m your-model.gguf --host 0.0.0.0 --port 8080 --jinja
#   add  --reasoning-format none   if it's a reasoning model (deepseek-r1, qwen3)
```
```powershell
# On your machine: tunnel the port, then run pointed at it
ssh -N -L 8080:gpu-node:8080 you@cluster       # llama.cpp defaults to 8080
cd D:\itksnap-agent
$env:AGENT_LLM="openai"
$env:LLM_BASE_URL="http://localhost:8080/v1"
$env:LLM_MODEL="your-model"                    # any string; /models shows the id
.\run.ps1
```
Or just click **⚙** in the UI, set Base URL `http://localhost:8080/v1`, hit
**Test /models**, then **Apply** — no restart. Watch the model's reasoning
stream, tool choices, and arguments live in the chat timeline: that is how you
see how it interprets each query.

Troubleshooting:
- **Model replies but never calls a tool** → you forgot `--jinja`, or the model
  has no tool template. Verify with `curl localhost:8080/v1/models`.
- **"streamed only reasoning" error** → relaunch with `--reasoning-format none`.
- **404** → the Base URL must end in `/v1` (not the web UI page).

## Layout
```
server/  config.py  llm.py  tools.py  agent.py  app.py  __main__.py
web/     index.html  app.js  style.css
```

## How this becomes "inside ITK-SNAP"
The tool *contract* (names + args) stays identical. Only two things change:
1. `tools.py` bodies get replaced by **QWebChannel** calls into ITK-SNAP's live
   viewer / Logic layer, and real models (nnInteractive, SAM2, a text-promptable
   model) replace `threshold_segment`.
2. `web/` is loaded by a **QWebEngineView** in a dock widget instead of the browser.

So everything you test here is the real agent + UI; embedding is a thin C++ shell.

## Roadmap
- [ ] `/v2/process_text_prompt` backend in a forked `itksnap-dls` (text -> mask)
- [ ] Token-level streaming from real backends
- [ ] Qt dock + AgentBridge (QWebChannel) in a fork of `pyushkevich/itksnap`
