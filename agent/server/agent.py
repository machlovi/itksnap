"""Backend-agnostic tool-use loop — same shape as RadAssistant's agent.

Neutral IR history; the backend does native function-calling. Each round:
  run_turn -> stream text/thinking -> if tool_use, execute each tool, append a
  tool_result message, loop; else finish. This is the faithful mirror of the
  cluster path, so what you observe here is how the model really interprets a
  query and chooses tools.

Emits structured events so the UI shows it live:
  status, token, thinking, tool_start, tool_result, viewer, assistant, error, turn_end
"""
from __future__ import annotations

import asyncio
import json

from config import config
from llm import make_backend


def _system_prompt() -> str:
    from skills import skills_index_block
    return (
        "You are the ITK-SNAP Assistant, embedded inside the ITK-SNAP medical image "
        "viewer. You help the user inspect, segment, and measure a 3D medical image by "
        "CALLING TOOLS that run inside ITK-SNAP. You cannot see the image directly; you "
        "act through the provided tools and read their results.\n"
        "Guidelines:\n"
        "- SKILLS FIRST. The AVAILABLE SKILLS listed below are vetted, expert multi-step "
        "procedures (active-contour segmentation, volumetry+reporting, multi-structure "
        "segmentation, cleanup, contrast/display, overlay comparison, exporting results). "
        "If the user's request matches a skill's description, your FIRST action MUST be to "
        "call use_skill with that skill's name, read the returned procedure, and then follow "
        "it step by step. Do NOT start calling segmentation/measurement/export tools for a "
        "multi-step request before loading the matching skill. Only skip skills for a trivial "
        "one-tool request that no skill covers (e.g. 'what's under the cursor').\n"
        "- If you are unsure of the current state, call get_scene_overview first.\n"
        "- To segment, use threshold_segment with an intensity range informed by the "
        "scene overview's reported min/max; the result is painted into the live viewer.\n"
        "- After segmenting, you can measure_volume and focus_label to show the user.\n"
        "- Take one tool action at a time. When the request is satisfied, reply with a "
        "short final message and no tool call.\n"
        "- If the user asks for SEVERAL things in one message (e.g. 'do X and Y and save Z "
        "to <path>'), you MUST complete EVERY requested action with its own tool call before "
        "finishing. Re-read the request and check nothing was skipped, especially save/export "
        "steps that write files.\n"
        "- Act, don't just inspect: after reading state, take the action the user asked for.\n"
        + skills_index_block()
    )


def _obs_to_text(obs: dict) -> tuple[str, bool]:
    """Turn a tool's observation dict into text the model reads + error flag."""
    is_error = obs.get("ok") is False or bool(obs.get("error"))
    if is_error:
        return obs.get("error", "unknown error"), True
    # message plus any structured extras the model may want to reason over
    extras = {k: v for k, v in obs.items()
              if k not in ("ok", "message", "viewer", "focus")}
    text = obs.get("message", "")
    if extras:
        text += "\n" + json.dumps(extras)
    return text.strip() or "done", False


class Agent:
    def __init__(self):
        self.backend = make_backend(config)
        self.system = _system_prompt()

    def rebuild_backend(self):
        self.backend = make_backend(config)

    async def run(self, tool_host, user_text: str, history: list[dict], emit):
        """Drive one user turn. `tool_host` supplies the tool schemas and
        executes tool calls (locally over SimpleITK, or remotely inside
        ITK-SNAP — the agent is identical either way)."""
        from skills import SKILL_TOOLS, get_skill, read_skill_file, match_skill
        history.append({"role": "user", "content": user_text})
        loop = asyncio.get_running_loop()
        retried_empty = False
        # client tools + the server-side skill tools (use_skill / read_skill_file)
        # for progressive disclosure of on-demand skill procedures + reference files
        tools = tool_host.definitions() + SKILL_TOOLS

        # Progressive disclosure that is robust to models which won't call use_skill
        # themselves: proactively load the ONE skill whose triggers match this request
        # and append its procedure to THIS turn's system prompt (history stays clean).
        # use_skill/read_skill_file remain available for capable models and for loading
        # a different skill or its reference files.
        turn_system = self.system
        matched = match_skill(user_text)
        if matched:
            body, _ = get_skill(matched)
            if body:
                turn_system = (
                    self.system
                    + "\n\n--- ACTIVE SKILL: " + matched + " ---\n"
                    "The user's request matches this vetted procedure. Follow it step by "
                    "step using the real tools. You may call read_skill_file for its "
                    "reference files, or use_skill to load a different skill.\n\n" + body)
                await emit({"type": "tool_start", "name": "use_skill",
                            "args": {"name": matched}})
                await emit({"type": "tool_result", "name": "use_skill", "ok": True,
                            "text": f"Loaded skill '{matched}'."})

        # thread-safe emit for streaming callbacks fired inside run_turn (worker thread)
        def stream(kind):
            def cb(delta):
                asyncio.run_coroutine_threadsafe(emit({"type": kind, "text": delta}), loop)
            return cb

        for _ in range(config.MAX_ROUNDS):
            await emit({"type": "status", "text": "thinking…"})
            result = await asyncio.to_thread(
                self.backend.run_turn, turn_system, history, tools,
                stream("token"), stream("thinking"))

            if result.error and not result.assistant_content:
                # llama.cpp reasoning models sometimes end a turn with only
                # reasoning and no answer — one silent retry usually recovers it.
                if not retried_empty:
                    retried_empty = True
                    await emit({"type": "status", "text": "empty response — retrying once…"})
                    continue
                await emit({"type": "error", "text": result.error})
                return

            history.append({"role": "assistant", "content": result.assistant_content})

            # surface any assistant text (mock/non-streaming backends)
            for block in result.assistant_content:
                if block.get("type") == "text" and block.get("text", "").strip():
                    await emit({"type": "assistant", "text": block["text"].strip()})

            if result.error:
                await emit({"type": "error", "text": result.error})
                return
            if result.stop_reason != "tool_use":
                return  # turn_end is emitted by the websocket handler

            tool_results = []
            for call in result.tool_calls:
                name, args = call["name"], dict(call.get("input") or {})
                await emit({"type": "tool_start", "name": name, "args": args})
                if name == "use_skill":
                    body, err = get_skill(args.get("name", ""))
                    obs = {"ok": err is None, "message": body} if body else {"ok": False, "error": err}
                elif name == "read_skill_file":
                    content, err = read_skill_file(args.get("name", ""), args.get("file", ""))
                    obs = {"ok": err is None, "message": content} if content else {"ok": False, "error": err}
                else:
                    obs = await tool_host.execute(name, args)

                text, is_error = _obs_to_text(obs)
                await emit({"type": "tool_result", "name": name,
                            "ok": not is_error, "text": text})
                if obs.get("viewer"):
                    await emit({"type": "viewer", "focus": obs.get("focus")})

                tool_results.append({"type": "tool_result", "tool_use_id": call["id"],
                                     "content": [{"type": "text", "text": text}],
                                     "is_error": is_error})
            history.append({"role": "user", "content": tool_results})

        await emit({"type": "error",
                    "text": f"Stopped after {config.MAX_ROUNDS} tool rounds."})
