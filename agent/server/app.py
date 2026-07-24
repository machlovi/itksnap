"""FastAPI app: serves the chat UI, a websocket per session, and a live slice PNG.

Run from the project root:
    uvicorn server.app:app --host 127.0.0.1 --port 8077
or just:  python -m server   (see server/__main__.py)
"""
from __future__ import annotations

import asyncio
import pathlib

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Response
from fastapi.responses import FileResponse

from config import config
from agent import Agent
from toolhost import LocalToolHost, RemoteToolHost
from tools import Session, render_png, ToolError

WEB_DIR = pathlib.Path(__file__).resolve().parent.parent / "web"

app = FastAPI(title="ITK-SNAP Agent MVP")
agent = Agent()

# in-memory session store: sid -> (Session, history)
_sessions: dict[str, tuple[Session, list]] = {}


def _get_session(sid: str) -> Session:
    if sid not in _sessions:
        _sessions[sid] = (Session(sid), [])
    return _sessions[sid][0]


@app.get("/")
def index():
    return FileResponse(WEB_DIR / "index.html")


@app.get("/static/{fname}")
def static_file(fname: str):
    # explicit routes for app.js / style.css (avoids an extra static-files dep)
    safe = pathlib.Path(fname).name
    path = WEB_DIR / safe
    if not path.exists():
        return Response(status_code=404)
    media = "text/javascript" if safe.endswith(".js") else "text/css"
    return FileResponse(path, media_type=media)


def _llm_state():
    return {"backend": config.LLM, "base_url": config.LLM_BASE_URL,
            "model": config.LLM_MODEL, "has_key": bool(config.LLM_API_KEY),
            "label": agent.backend.label}


@app.get("/api/config")
def api_config():
    return _llm_state()


@app.get("/api/llm")
def get_llm():
    return _llm_state()


@app.post("/api/llm")
async def set_llm(payload: dict):
    """Point the agent at a different model at runtime (no restart).

    Body: {backend: "mock"|"openai", base_url, model, api_key}
    Use this to attach your cluster model via a localhost port-forward, e.g.
    {"backend":"openai","base_url":"http://localhost:8000/v1","model":"Qwen2.5-7B-Instruct"}
    """
    backend = (payload.get("backend") or config.LLM).lower()
    config.LLM = "openai" if backend == "openai" else "mock"
    if "base_url" in payload:
        config.LLM_BASE_URL = payload["base_url"].strip()
    if "model" in payload:
        config.LLM_MODEL = payload["model"].strip()
    if "api_key" in payload:
        config.LLM_API_KEY = payload["api_key"].strip()
    agent.rebuild_backend()
    return _llm_state()


@app.get("/api/llm/ping")
def ping_llm():
    """Check the configured OpenAI-compatible endpoint is reachable (GET /models)."""
    import urllib.request, urllib.error, json as _json
    from llm import normalize_v1_base
    if config.LLM != "openai":
        return {"ok": True, "detail": "mock backend (no network)"}
    url = normalize_v1_base(config.LLM_BASE_URL) + "/models"
    try:
        headers = {}
        if config.LLM_API_KEY:
            headers["Authorization"] = "Bearer " + config.LLM_API_KEY
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=8) as r:
            data = _json.loads(r.read().decode("utf-8", "replace"))
        models = [m.get("id") for m in data.get("data", [])] if isinstance(data, dict) else []
        return {"ok": True, "url": url, "models": models[:20]}
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "url": url, "error": str(e)}


@app.get("/api/session/{sid}/slice.png")
def slice_png(sid: str, axis: str = "axial", index: int | None = None, overlay: int = 1):
    session = _get_session(sid)
    try:
        png = render_png(session, axis=axis, index=index, overlay=bool(overlay))
    except ToolError:
        return Response(status_code=204)  # no image yet
    return Response(content=png, media_type="image/png",
                    headers={"Cache-Control": "no-store"})


@app.get("/api/session/{sid}/info")
def slice_info(sid: str, axis: str = "axial"):
    from tools import AXES
    session = _get_session(sid)
    if session.image is None:
        return {"has_image": False}
    dims = session.image.GetSize()[::-1]  # numpy order (z,y,x)
    ax = AXES.get(axis.lower(), 0)
    return {"has_image": True, "n_slices": dims[ax],
            "has_labels": session.labels is not None}


@app.websocket("/ws/{sid}")
async def ws(websocket: WebSocket, sid: str):
    """LOCAL mode: tools run in-process over SimpleITK (browser test app)."""
    await websocket.accept()
    session, history = _sessions.setdefault(sid, (Session(sid), []))[0], _sessions[sid][1]
    host = LocalToolHost(session)

    async def emit(event: dict):
        await websocket.send_json(event)

    await emit({"type": "ready", "mode": "local", "llm": config.LLM})
    try:
        while True:
            data = await websocket.receive_json()
            text = (data or {}).get("text", "").strip()
            if not text:
                continue
            await emit({"type": "user", "text": text})
            await agent.run(host, text, history, emit)
            await emit({"type": "turn_end"})
    except WebSocketDisconnect:
        pass


@app.websocket("/wsbridge/{sid}")
async def wsbridge(websocket: WebSocket, sid: str):
    """EMBEDDING protocol: the CLIENT (ITK-SNAP) hosts the tools.

    Client -> server: {type:"hello", tools:[<schemas>]}, {type:"user", text},
                      {type:"tool_result", id, ok, text}
    Server -> client: {type:"ready"|"user"|"status"|"token"|"thinking"|
                       "tool_start"|"tool_result"|"assistant"|"error"|"turn_end"},
                      {type:"tool_call", id, name, args}   <- execute in ITK-SNAP
    """
    await websocket.accept()
    history: list = []

    async def send(obj: dict):
        await websocket.send_json(obj)

    host = RemoteToolHost(send)
    active = {"turn": None}
    await send({"type": "ready", "mode": "remote", "llm": config.LLM})
    try:
        while True:
            msg = await websocket.receive_json()
            mtype = (msg or {}).get("type")

            if mtype == "hello":
                host.set_tools(msg.get("tools") or [])
                await send({"type": "hello_ack", "n_tools": len(host.definitions())})

            elif mtype == "set_llm":
                # dock sets which LLM the agent talks to (full URL or bare host:port)
                config.LLM = "openai"
                if msg.get("base_url"):
                    config.LLM_BASE_URL = str(msg["base_url"]).strip()
                if msg.get("model"):
                    config.LLM_MODEL = str(msg["model"]).strip()
                if "api_key" in msg:
                    config.LLM_API_KEY = str(msg["api_key"]).strip()
                agent.rebuild_backend()
                await send({"type": "llm_set", "label": agent.backend.label})

            elif mtype == "tool_result":
                host.resolve(msg.get("id"), msg)

            elif mtype == "user":
                text = (msg.get("text") or "").strip()
                if not text:
                    continue
                if active["turn"] and not active["turn"].done():
                    await send({"type": "error", "text": "A turn is already running."})
                    continue
                if not host.definitions():
                    await send({"type": "error", "text": "No tools registered — send a 'hello' with tool schemas first."})
                    continue
                await send({"type": "user", "text": text})

                async def do_turn(t=text):
                    try:
                        await agent.run(host, t, history, send)
                    except Exception as e:  # noqa: BLE001
                        await send({"type": "error", "text": f"agent error: {e}"})
                    await send({"type": "turn_end"})

                active["turn"] = asyncio.create_task(do_turn())
    except WebSocketDisconnect:
        host.fail_all("client disconnected")
