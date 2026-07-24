"""Backend-agnostic LLM layer supporting local endpoints & cloud API providers.

Supports:
  - Local OpenAI-compatible (/v1): Ollama, vLLM, llama.cpp, LM Studio, Qwen, DeepSeek
  - OpenAI Cloud: https://api.openai.com/v1 (gpt-4o, gpt-4o-mini, o3-mini)
  - Anthropic Cloud: https://api.anthropic.com/v1 (claude-3-5-sonnet-20241022, claude-3-5-haiku)
  - Google Gemini: https://generativelanguage.googleapis.com/v1beta/openai/ (gemini-2.0-flash)
  - Groq Cloud: https://api.groq.com/openai/v1 (llama-3.3-70b-versatile)
  - DeepSeek Cloud: https://api.deepseek.com/v1 (deepseek-chat)
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
    """Return the API root ending in /v1, forgiving common URL paste variations."""
    base = (base_url or "").strip().rstrip("/")
    if base and not base.startswith(("http://", "https://")):
        base = "http://" + base
    if base.endswith("/chat/completions"):
        base = base[: -len("/chat/completions")]
    if base.endswith("/messages"):
        base = base[: -len("/messages")]
    if not base.endswith("/v1") and not "/v1beta" in base:
        base = base + "/v1"
    return base


# ----------------------------------------------------------------------
# OpenAI-compatible backend — Local (vLLM/llama.cpp/Ollama) & Cloud APIs
# (OpenAI, Gemini, Groq, DeepSeek, Together, LM Studio)
# ----------------------------------------------------------------------
class OpenAICompatBackend:
    def __init__(self, base_url, model="", api_key=None, timeout=600):
        root = normalize_v1_base(base_url)
        self._url = root + "/chat/completions"
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
            if exc.code == 401:
                return TurnResult([], "error", error=f"Authentication error (401). Please check your API key.")
            if exc.code == 404:
                return TurnResult([], "error", error=(
                    f"404 at {self._url}. Verify endpoint URL and model ID."))
            return TurnResult([], "error", error=f"Server HTTP {exc.code}: {detail}")
        except urllib.error.URLError as exc:
            return TurnResult([], "error",
                              error=f"Could not reach {self._url} ({exc.reason}). "
                                    "Is the endpoint URL correct?")
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
                    return TurnResult([], "error", error=f"API error: {detail}")
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
            return TurnResult([], "error", error="Empty turn received from model endpoint.")
        return TurnResult(assistant_content, stop_reason, tool_calls, usage)


# ----------------------------------------------------------------------
# Anthropic Cloud Messages API Backend (Claude 3.5 Sonnet / Haiku)
# ----------------------------------------------------------------------
class AnthropicBackend:
    def __init__(self, base_url="https://api.anthropic.com/v1", model="claude-3-5-sonnet-20241022", api_key=None, timeout=600):
        self._url = "https://api.anthropic.com/v1/messages"
        self._model = model or "claude-3-5-sonnet-20241022"
        self._api_key = api_key or None
        self._timeout = timeout

    @property
    def label(self):
        return f"anthropic:{self._model} @ {self._url}"

    def run_turn(self, system, messages, tools, on_text=None, on_thinking=None):
        if not self._api_key:
            return TurnResult([], "error", error="Anthropic API key required. Enter your API Key in the LLM settings.")

        headers = {
            "x-api-key": self._api_key,
            "anthropic-version": "2023-06-01",
            "content-type": "application/json"
        }

        anthropic_tools = []
        for t in tools:
            anthropic_tools.append({
                "name": t["name"],
                "description": t["description"],
                "input_schema": t["input_schema"]
            })

        body = {
            "model": self._model,
            "system": system,
            "messages": messages,
            "tools": anthropic_tools,
            "max_tokens": 4096,
            "stream": True
        }

        req = urllib.request.Request(self._url, data=json.dumps(body).encode(),
                                     headers=headers, method="POST")

        text_out = []
        tool_calls = []
        current_tool = None

        try:
            response = urllib.request.urlopen(req, timeout=self._timeout)
            for raw in response:
                line = raw.decode("utf-8", "replace").strip()
                if not line or not line.startswith("data:"):
                    continue
                payload = line[5:].strip()
                if payload == "[DONE]":
                    break
                try:
                    event = json.loads(payload)
                except json.JSONDecodeError:
                    continue

                etype = event.get("type")
                if etype == "content_block_delta":
                    delta = event.get("delta", {})
                    if delta.get("type") == "text_delta":
                        t = delta.get("text", "")
                        text_out.append(t)
                        if on_text:
                            on_text(t)
                    elif delta.get("type") == "input_json_delta":
                        if current_tool:
                            current_tool["json_buf"] += delta.get("partial_json", "")
                elif etype == "content_block_start":
                    block = event.get("content_block", {})
                    if block.get("type") == "tool_use":
                        current_tool = {
                            "id": block.get("id"),
                            "name": block.get("name"),
                            "json_buf": ""
                        }
                        tool_calls.append(current_tool)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")[:300]
            return TurnResult([], "error", error=f"Anthropic API error ({exc.code}): {detail}")
        except Exception as exc:
            return TurnResult([], "error", error=f"Anthropic request failed: {exc}")

        # Parse tool call JSONs
        final_tool_calls = []
        for tc in tool_calls:
            try:
                args = json.loads(tc["json_buf"]) if tc["json_buf"] else {}
            except json.JSONDecodeError:
                args = {}
            final_tool_calls.append({"id": tc["id"], "name": tc["name"], "input": args})

        assistant_content = []
        joined = "".join(text_out)
        if joined.strip():
            assistant_content.append({"type": "text", "text": joined})
        for call in final_tool_calls:
            assistant_content.append({"type": "tool_use", "id": call["id"],
                                      "name": call["name"], "input": call["input"]})

        stop_reason = "tool_use" if final_tool_calls else "end"
        return TurnResult(assistant_content, stop_reason, final_tool_calls)


# ----------------------------------------------------------------------
# Mock Backend (Offline Fallback)
# ----------------------------------------------------------------------
class MockBackend:
    label = "mock"

    def run_turn(self, system, messages, tools, on_text=None, on_thinking=None):
        if on_text:
            on_text("Mock LLM ready.")
        return TurnResult([{"type": "text", "text": "Mock backend active."}], "end")


# ----------------------------------------------------------------------
def make_backend(cfg):
    url = (cfg.LLM_BASE_URL or "").lower()
    if "anthropic.com" in url:
        return AnthropicBackend(cfg.LLM_BASE_URL, cfg.LLM_MODEL, cfg.LLM_API_KEY, cfg.LLM_TIMEOUT)
    if cfg.LLM == "openai" or url:
        return OpenAICompatBackend(cfg.LLM_BASE_URL, cfg.LLM_MODEL,
                                   cfg.LLM_API_KEY, cfg.LLM_TIMEOUT)
    return MockBackend()
