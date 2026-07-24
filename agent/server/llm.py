"""Backend-agnostic LLM layer — same design as RadAssistant.

The agent speaks ONE neutral message IR (Anthropic-style content blocks) and
never imports a vendor SDK. Each backend translates that IR to its wire format,
runs a request, and returns a TurnResult. This mirrors RadAssistant so that live
testing here reflects exactly how the cluster model interprets queries + calls
tools in the real system.

Neutral messages the agent produces:
  {"role":"user","content":"<text>"}
  {"role":"assistant","content":[{"type":"text","text":..},
                                 {"type":"tool_use","id","name","input"}]}
  {"role":"user","content":[{"type":"tool_result","tool_use_id":..,
                             "content":[{"type":"text","text":..}],"is_error":bool}]}

Tools: [{"name","description","input_schema": <JSON schema>}]
"""
from __future__ import annotations

import json
import urllib.error
import urllib.request


class TurnResult:
    def __init__(self, assistant_content, stop_reason, tool_calls=None, usage=None, error=None):
        self.assistant_content = assistant_content      # list of blocks
        self.stop_reason = stop_reason                  # "end" | "tool_use" | "error"
        self.tool_calls = tool_calls or []              # [{"id","name","input"}]
        self.usage = usage or {}
        self.error = error


def _iter_blocks(content):
    if isinstance(content, str):
        yield {"type": "text", "text": content}
    elif isinstance(content, list):
        yield from content


def normalize_v1_base(base_url):
    """Return the OpenAI API root ending in /v1, forgiving common paste mistakes.

    'http://host:11440'                   -> 'http://host:11440/v1'
    'http://host:11440/'                  -> 'http://host:11440/v1'
    'http://host:11440/v1'                -> 'http://host:11440/v1'
    'http://host:11440/v1/chat/completions' -> 'http://host:11440/v1'
    """
    base = (base_url or "").strip().rstrip("/")
    if base and not base.startswith(("http://", "https://")):
        base = "http://" + base            # accept a bare host:port
    if base.endswith("/chat/completions"):
        base = base[: -len("/chat/completions")]
    if not base.endswith("/v1"):
        base = base + "/v1"
    return base


# ----------------------------------------------------------------------
# OpenAI-compatible backend — vLLM / TGI / llama.cpp / LM Studio / Ollama /v1.
# This is the one you point at your cluster (via a localhost port-forward).
# Faithful to RadAssistant's OpenAICompatBackend, trimmed for the MVP.
# ----------------------------------------------------------------------
class OpenAICompatBackend:
    def __init__(self, base_url, model="", api_key=None, timeout=600):
        self._url = normalize_v1_base(base_url) + "/chat/completions"
        self._model = model
        self._api_key = api_key or None
        self._timeout = timeout

    @property
    def label(self):
        return f"openai-compat:{self._model or '?'} @ {self._url}"

    def _translate(self, system, messages):
        wire = [{"role": "system", "content": system}]
        id_to_name = {}
        for message in messages:
            role, content = message.get("role"), message.get("content")

            if role == "assistant":
                text_parts, tool_calls = [], []
                for block in _iter_blocks(content):
                    if block.get("type") == "text":
                        text_parts.append(block.get("text", ""))
                    elif block.get("type") == "tool_use":
                        id_to_name[block.get("id")] = block.get("name")
                        tool_calls.append({
                            "id": block.get("id"), "type": "function",
                            "function": {"name": block.get("name"),
                                         "arguments": json.dumps(block.get("input", {}))}})
                entry = {"role": "assistant", "content": "\n".join(text_parts)}
                if tool_calls:
                    entry["tool_calls"] = tool_calls
                wire.append(entry)
                continue

            if isinstance(content, str):
                wire.append({"role": "user", "content": content})
                continue

            for block in _iter_blocks(content):
                if block.get("type") == "tool_result":
                    inner = block.get("content", [])
                    text = inner if isinstance(inner, str) else "\n".join(
                        p.get("text", "") for p in inner if p.get("type") == "text")
                    if block.get("is_error"):
                        text = "[tool error] " + text
                    wire.append({"role": "tool",
                                 "tool_call_id": block.get("tool_use_id"),
                                 "name": id_to_name.get(block.get("tool_use_id"), ""),
                                 "content": text or "(no output)"})
                elif block.get("type") == "text":
                    wire.append({"role": "user", "content": block.get("text", "")})
        return wire

    def run_turn(self, system, messages, tools, on_text=None, on_thinking=None):
        body = {
            "model": self._model,
            "messages": self._translate(system, messages),
            "tools": [{"type": "function", "function": {
                "name": t["name"], "description": t["description"],
                "parameters": t["input_schema"]}} for t in tools],
            "stream": True,
            "stream_options": {"include_usage": True},
        }
        headers = {"Content-Type": "application/json"}
        if self._api_key:
            headers["Authorization"] = "Bearer " + self._api_key
        req = urllib.request.Request(self._url, data=json.dumps(body).encode(),
                                     headers=headers, method="POST")

        text_out, thinking_out, calls, usage = [], [], {}, {}
        stop_reason = "end"
        try:
            response = urllib.request.urlopen(req, timeout=self._timeout)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")[:300]
            if exc.code == 404:
                return TurnResult([], "error", error=(
                    f"404 at {self._url}. The Base URL must end in /v1 "
                    "(e.g. http://localhost:8000/v1). Verify: curl <base>/models"))
            return TurnResult([], "error", error=f"Server HTTP {exc.code}: {detail}")
        except urllib.error.URLError as exc:
            return TurnResult([], "error",
                              error=f"Could not reach {self._url} ({exc.reason}). "
                                    "Is the tunnel/server up?")
        try:
            for raw in response:
                line = raw.decode("utf-8", "replace").strip()
                if not line or not line.startswith("data:"):
                    continue
                payload = line[5:].strip()
                if payload == "[DONE]":
                    break
                try:
                    chunk = json.loads(payload)
                except json.JSONDecodeError:
                    continue
                if "error" in chunk:
                    err = chunk["error"]
                    detail = err.get("message") if isinstance(err, dict) else str(err)
                    return TurnResult([], "error", error=f"Server error mid-stream: {detail}")
                if chunk.get("usage"):
                    usage = {"input": chunk["usage"].get("prompt_tokens", 0),
                             "output": chunk["usage"].get("completion_tokens", 0)}
                for choice in chunk.get("choices", []):
                    delta = choice.get("delta", {})
                    piece = delta.get("content")
                    if piece:
                        text_out.append(piece)
                        if on_text:
                            on_text(piece)
                    thought = delta.get("reasoning_content") or delta.get("reasoning")
                    if thought:
                        thinking_out.append(thought)
                        if on_thinking:
                            on_thinking(thought)
                    for tc in delta.get("tool_calls", []) or []:
                        idx = tc.get("index", 0)
                        slot = calls.setdefault(idx, {"id": None, "name": None, "args": ""})
                        if tc.get("id"):
                            slot["id"] = tc["id"]
                        fn = tc.get("function", {})
                        if fn.get("name"):
                            slot["name"] = fn["name"]
                        if fn.get("arguments"):
                            slot["args"] += fn["arguments"]
                    if choice.get("finish_reason") == "tool_calls":
                        stop_reason = "tool_use"
        except (urllib.error.URLError, ConnectionError, TimeoutError) as exc:
            return TurnResult([], "error", error=f"Stream dropped: {exc}")
        finally:
            response.close()

        tool_calls = []
        for idx in sorted(calls):
            slot = calls[idx]
            try:
                args = json.loads(slot["args"]) if slot["args"] else {}
            except json.JSONDecodeError:
                args = {}
            tool_calls.append({"id": slot["id"] or f"call_{idx}",
                               "name": slot["name"], "input": args})

        assistant_content = []
        joined = "".join(text_out)
        if joined.strip():
            assistant_content.append({"type": "text", "text": joined})
        for call in tool_calls:
            assistant_content.append({"type": "tool_use", "id": call["id"],
                                      "name": call["name"], "input": call["input"]})
        if tool_calls:
            stop_reason = "tool_use"
        if not assistant_content and not tool_calls:
            if thinking_out:
                return TurnResult([], "error", error=(
                    "The model streamed only reasoning and no answer/tool call. On llama.cpp "
                    "relaunch llama-server with --reasoning-format none (a retry may also help)."))
            return TurnResult([], "error", error=(
                "Empty turn, no tool called. If the model never calls tools, launch llama-server "
                "with --jinja and use a tool-capable instruct model (Qwen2.5-Instruct, "
                "Llama-3.1-Instruct, Hermes, functionary)."))
        return TurnResult(assistant_content, stop_reason, tool_calls, usage)


# ----------------------------------------------------------------------
# Mock backend — deterministic keyword planner, native-tools shape.
# No network, no keys: exercises the full loop offline.
# ----------------------------------------------------------------------
class MockBackend:
    label = "mock"

    def run_turn(self, system, messages, tools, on_text=None, on_thinking=None):
        user = self._last_user(messages).lower()
        done = self._tools_done(messages)
        has_image = self._has_image(messages)

        def tool(tool_name, **args):
            cid = f"call_{len(done)}"
            return TurnResult([{"type": "tool_use", "id": cid,
                                "name": tool_name, "input": args}], "tool_use",
                              [{"id": cid, "name": tool_name, "input": args}])

        def final(text):
            if on_text:
                on_text(text)
            return TurnResult([{"type": "text", "text": text}], "end")

        if any(w in user for w in ("volume", "how big", "size", " ml")):
            if "compute_volume" not in done:
                lid = 2 if "organ" in user else 1
                return tool("compute_volume", label=lid)
            return final("Done — see the measurement above.")

        if any(w in user for w in ("segment", "threshold", "nodule", "tumor", "organ", "bright", "mask")):
            if not has_image and "load_demo" not in done and "load_image" not in done:
                return tool("load_demo")
            if "threshold_segment" not in done:
                if any(w in user for w in ("organ", "kidney", "structure")):
                    return tool("threshold_segment", lower=80, upper=160, label=2, name="organ")
                return tool("threshold_segment", lower=180, label=1, name="nodule")
            if "show_slice" not in done:
                return tool("show_slice", axis="axial")
            return final("Segmentation done and shown in the viewer.")

        if any(w in user for w in ("demo", "phantom", "sample", "load", "open")):
            if "load_demo" not in done:
                return tool("load_demo")
            return final("Demo loaded. Try 'segment the bright nodule'.")

        if any(w in user for w in ("show", "view", "slice", "look")):
            if "show_slice" not in done:
                ax = "coronal" if "coronal" in user else "sagittal" if "sagittal" in user else "axial"
                return tool("show_slice", axis=ax)
            return final("There you go.")

        return final("I can load a demo volume, segment a structure by intensity, "
                     "measure its volume, and show slices. Try: 'load demo'.")

    @staticmethod
    def _last_user(messages):
        for m in reversed(messages):
            if m.get("role") == "user" and isinstance(m.get("content"), str):
                return m["content"]
        return ""

    @staticmethod
    def _tools_done(messages):
        done = []
        for m in reversed(messages):
            if m.get("role") == "user" and isinstance(m.get("content"), str):
                break
            if m.get("role") == "assistant" and isinstance(m.get("content"), list):
                done += [b.get("name") for b in m["content"] if b.get("type") == "tool_use"]
        return done

    @staticmethod
    def _has_image(messages):
        for m in messages:
            content = m.get("content")
            if isinstance(content, list):
                for b in content:
                    if b.get("type") == "tool_result":
                        inner = b.get("content", [])
                        text = inner if isinstance(inner, str) else " ".join(
                            p.get("text", "") for p in inner)
                        if '"size"' in text or "phantom" in text or "Loaded" in text:
                            return True
        return False


# ----------------------------------------------------------------------
def make_backend(cfg):
    if cfg.LLM == "openai":
        return OpenAICompatBackend(cfg.LLM_BASE_URL, cfg.LLM_MODEL,
                                   cfg.LLM_API_KEY, cfg.LLM_TIMEOUT)
    return MockBackend()
