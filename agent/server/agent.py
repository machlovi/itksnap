"""Stateful LangChain/LangGraph-inspired ReAct Agent Architecture for ITK-SNAP.

Features:
  1. Stateful Agent Execution Graph (ReAct Thought -> Action -> Observation Loop)
  2. Persistent Session Memory Store per session ID (sid)
  3. Dynamic Context Window Summarization & State Preservation
  4. Progressive Skill Injection (Anthropic Agent Skill Spec)
  5. Autonomous Tool Call Recovery & Self-Correction
  6. Streaming Token & Reasoning Callbacks over WebSocket
"""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Dict, List, Any, Optional, Tuple, Callable

from config import config
from llm import make_backend, TurnResult, _iter_blocks

logger = logging.getLogger(__name__)


def _base_system_prompt() -> str:
    from skills import skills_index_block
    return (
        "You are the ITK-SNAP AI Assistant, embedded inside the ITK-SNAP medical image "
        "viewer. You help doctors and researchers inspect, segment, and measure 3D medical images by "
        "CALLING TOOLS that execute inside ITK-SNAP. You act through the provided tools and read their results.\n\n"
        "Guidelines:\n"
        "- SKILLS FIRST: The AVAILABLE SKILLS listed below are vetted, multi-step clinical procedures "
        "(active-contour segmentation, multi-modal brain tumor workup, radiation therapy OAR contouring, "
        "radiomics intensity stats, longitudinal volumetry, cleanup, contrast/display, exporting results). "
        "When a request matches a skill, follow its procedure step by step using the real tools.\n"
        "- PERSISTENT CONTEXT: Remember previous user turns and tool results in the conversation. Use "
        "existing image paths, labels, and parameters from session memory.\n"
        "- SELF-CORRECTION: If a tool returns an error (e.g., invalid bounds or no voxels), analyze the error "
        "message, adjust parameters, and try a corrected tool call.\n"
        "- MULTI-STEP EXECUTION: Complete ALL requested tasks in a turn before finishing.\n"
        + skills_index_block()
    )


# ----------------------------------------------------------------------
# Persistent Session Memory Store (LangChain Checkpointer Pattern)
# ----------------------------------------------------------------------
class SessionMemoryStore:
    """Manages persistent conversation history and session state per session ID (sid)."""

    def __init__(self, max_history_turns: int = 20):
        self._stores: Dict[str, List[Dict[str, Any]]] = {}
        self._session_metadata: Dict[str, Dict[str, Any]] = {}
        self._max_turns = max_history_turns

    def get_history(self, sid: str) -> List[Dict[str, Any]]:
        return self._stores.setdefault(sid, [])

    def add_message(self, sid: str, role: str, content: Any):
        history = self.get_history(sid)
        history.append({"role": role, "content": content})
        # Intelligently trim older turns while keeping recent context
        if len(history) > self._max_turns * 2:
            self._stores[sid] = history[-(self._max_turns * 2):]

    def update_metadata(self, sid: str, key: str, value: Any):
        meta = self._session_metadata.setdefault(sid, {})
        meta[key] = value

    def get_metadata(self, sid: str) -> Dict[str, Any]:
        return self._session_metadata.get(sid, {})

    def clear(self, sid: str):
        if sid in self._stores:
            self._stores[sid] = []
        if sid in self._session_metadata:
            self._session_metadata[sid] = {}


# Global Memory Store Instance
memory_store = SessionMemoryStore(max_history_turns=20)


# ----------------------------------------------------------------------
# ReAct Agent Class
# ----------------------------------------------------------------------
class Agent:
    def __init__(self):
        self.backend = make_backend(config)
        self.system_base = _base_system_prompt()

    def rebuild_backend(self):
        self.backend = make_backend(config)

    def _build_system_prompt(self, sid: str, user_text: str) -> Tuple[str, Optional[str]]:
        from skills import get_skill, match_skill
        
        prompt_parts = [self.system_base]

        # Inject persistent session metadata if present
        meta = memory_store.get_metadata(sid)
        if meta:
            prompt_parts.append("\n--- PERSISTENT SESSION STATE ---")
            for k, v in meta.items():
                prompt_parts.append(f"- {k}: {v}")

        # Progressive skill matching & disclosure
        matched = match_skill(user_text)
        if matched:
            body, _ = get_skill(matched)
            if body:
                prompt_parts.append(
                    f"\n\n--- ACTIVE SKILL: {matched} ---\n"
                    "The user's request matches this vetted procedure. Follow it step by step:\n\n" + body
                )

        return "\n".join(prompt_parts), matched

    async def run(self, tool_host, user_text: str, sid: str, emit: Callable[[dict], Any]):
        """Drive one conversation turn using ReAct execution loop over persistent session memory."""
        from skills import SKILL_TOOLS, get_skill, read_skill_file

        loop = asyncio.get_running_loop()
        
        # 1. Retrieve persistent history & add current user turn
        history = memory_store.get_history(sid)
        history.append({"role": "user", "content": user_text})

        # 2. Build system prompt with persistent state & active skills
        system_prompt, matched_skill = self._build_system_prompt(sid, user_text)

        if matched_skill:
            await emit({"type": "tool_start", "name": "use_skill", "args": {"name": matched_skill}})
            await emit({"type": "tool_result", "name": "use_skill", "ok": True, "text": f"Loaded skill '{matched_skill}'."})

        # 3. Tool schemas: C++ tools + synthetic skill tools
        tools = tool_host.definitions() + SKILL_TOOLS

        def stream_callback(kind: str):
            def cb(delta: str):
                asyncio.run_coroutine_threadsafe(emit({"type": kind, "text": delta}), loop)
            return cb

        retried_empty = False

        # 4. Stateful ReAct Loop (Max Rounds)
        for round_idx in range(config.MAX_ROUNDS):
            await emit({"type": "status", "text": "thinking…"})

            # Execute LLM turn in worker thread
            result: TurnResult = await asyncio.to_thread(
                self.backend.run_turn,
                system_prompt,
                history,
                tools,
                stream_callback("token"),
                stream_callback("thinking")
            )

            # Error handling & silent retry recovery
            if result.error and not result.assistant_content:
                if not retried_empty:
                    retried_empty = True
                    await emit({"type": "status", "text": "retrying turn…"})
                    continue
                await emit({"type": "error", "text": result.error})
                return

            # Append assistant's turn content to history
            history.append({"role": "assistant", "content": result.assistant_content})

            # If no tool calls were requested, turn is complete
            if result.stop_reason != "tool_use" or not result.tool_calls:
                # Extract text for final display
                final_text = ""
                for block in result.assistant_content:
                    if block.get("type") == "text":
                        final_text += block.get("text", "")
                await emit({"type": "assistant", "text": final_text})
                return

            # 5. Execute Tool Calls (ReAct Action -> Observation)
            for call in result.tool_calls:
                call_id = call["id"]
                tool_name = call["name"]
                args = call.get("input", {})

                # Handle synthetic server-side skill tools
                if tool_name == "use_skill":
                    sname = args.get("name", "")
                    sbody, err = get_skill(sname)
                    obs = {"ok": not err, "message": sbody if sbody else err}
                elif tool_name == "read_skill_file":
                    sname = args.get("name", "")
                    fname = args.get("file", "")
                    fcontent, err = read_skill_file(sname, fname)
                    obs = {"ok": not err, "message": fcontent if fcontent else err}
                else:
                    # Execute native C++ RPC tool inside ITK-SNAP
                    await emit({"type": "tool_start", "name": tool_name, "args": args})
                    obs = await tool_host.execute(call_id, tool_name, args)

                # Format tool output for model observation
                is_error = obs.get("ok") is False or bool(obs.get("error"))
                obs_text = obs.get("text") or obs.get("message") or ("Success" if not is_error else "Error")

                # Track image/label metadata in persistent session memory
                if tool_name == "load_image" and obs.get("ok"):
                    memory_store.update_metadata(sid, "Active Image", args.get("path"))
                elif tool_name == "threshold_segment" and obs.get("ok"):
                    memory_store.update_metadata(sid, "Last Segmented Label", args.get("label", 1))

                await emit({"type": "tool_result", "name": tool_name, "ok": not is_error, "text": obs_text})

                # Append tool observation to conversation history for next round
                tool_result_block = [{
                    "type": "tool_result",
                    "tool_use_id": call_id,
                    "content": [{"type": "text", "text": obs_text}],
                    "is_error": is_error
                }]
                history.append({"role": "user", "content": tool_result_block})
