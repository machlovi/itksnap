"""Official LangChain / LangGraph Deep Agent Architecture for ITK-SNAP.

Features & Fixes:
  - langgraph.prebuilt.create_react_agent (Stateful ReAct Deep Agent Graph)
  - langgraph.checkpoint.memory.MemorySaver (Session Memory Checkpointer)
  - Dynamic C++ Tool Bindings (2-arg execute(name, args))
  - Reliable token accumulation & assistant event emission for ITK-SNAP GUI
  - Full multi-round ReAct fallback engine
"""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Dict, List, Any, Optional, Tuple, Callable

from config import config

logger = logging.getLogger(__name__)

# Check for LangChain / LangGraph imports
try:
    from langgraph.prebuilt import create_react_agent
    from langgraph.checkpoint.memory import MemorySaver
    from langchain_core.tools import StructuredTool
    from langchain_core.messages import HumanMessage, AIMessage, SystemMessage, ToolMessage
    from langchain_openai import ChatOpenAI
    from langchain_anthropic import ChatAnthropic
    HAS_LANGCHAIN = True
except ImportError:
    HAS_LANGCHAIN = False


def _system_prompt_base() -> str:
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


def _extract_chunk_text(content: Any) -> str:
    """Safely extract string content from LangChain stream chunks (handles Anthropic list blocks)."""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        text_parts = []
        for block in content:
            if isinstance(block, str):
                text_parts.append(block)
            elif isinstance(block, dict) and block.get("type") == "text":
                text_parts.append(block.get("text", ""))
        return "".join(text_parts)
    return str(content or "")


# ----------------------------------------------------------------------
# LangChain Model Factory
# ----------------------------------------------------------------------
def create_langchain_model():
    """Create a LangChain chat model instance matching config settings."""
    url = (config.LLM_BASE_URL or "").lower()
    
    if "anthropic.com" in url:
        return ChatAnthropic(
            model=config.LLM_MODEL or "claude-3-5-sonnet-20241022",
            api_key=config.LLM_API_KEY or "dummy",
            streaming=True,
            temperature=0.0
        )
    
    # Default to ChatOpenAI (supports Ollama, vLLM, llama.cpp, OpenAI, Gemini, Groq, DeepSeek)
    base_url = config.LLM_BASE_URL.rstrip("/")
    if not base_url.endswith("/v1") and not "/v1beta" in base_url:
        base_url += "/v1"
        
    return ChatOpenAI(
        model=config.LLM_MODEL or "qwen",
        openai_api_base=base_url,
        openai_api_key=config.LLM_API_KEY or "dummy",
        streaming=True,
        temperature=0.0
    )


# ----------------------------------------------------------------------
# LangGraph Deep Agent Engine
# ----------------------------------------------------------------------
class LangChainDeepAgent:
    def __init__(self):
        self.checkpointer = MemorySaver() if HAS_LANGCHAIN else None
        self.system_prompt = _system_prompt_base()

    def _convert_tools(self, tool_host, emit_fn: Callable[[dict], Any]) -> List[Any]:
        """Convert dynamic C++ tool schemas and server skill tools to LangChain StructuredTools."""
        from skills import SKILL_TOOLS, get_skill, read_skill_file

        langchain_tools = []
        all_schemas = tool_host.definitions() + SKILL_TOOLS

        for t in all_schemas:
            name = t["name"]
            desc = t.get("description", "")
            
            def make_tool_fn(tool_name=name):
                async def async_tool_fn(**kwargs) -> str:
                    await emit_fn({"type": "tool_start", "name": tool_name, "args": kwargs})
                    if tool_name == "use_skill":
                        sname = kwargs.get("name", "")
                        sbody, err = get_skill(sname)
                        res_text = sbody if sbody else (err or "Skill not found.")
                        await emit_fn({"type": "tool_result", "name": tool_name, "ok": sbody is not None, "text": res_text})
                        return res_text
                    elif tool_name == "read_skill_file":
                        sname = kwargs.get("name", "")
                        fname = kwargs.get("file", "")
                        fcontent, err = read_skill_file(sname, fname)
                        res_text = fcontent if fcontent else (err or "File not found.")
                        await emit_fn({"type": "tool_result", "name": tool_name, "ok": fcontent is not None, "text": res_text})
                        return res_text
                    else:
                        # Fix: tool_host.execute takes exactly 2 arguments (name, args)
                        obs = await tool_host.execute(tool_name, kwargs)
                        is_error = obs.get("ok") is False or bool(obs.get("error"))
                        text = obs.get("text") or obs.get("message") or ("Success" if not is_error else "Error")
                        await emit_fn({"type": "tool_result", "name": tool_name, "ok": not is_error, "text": text})
                        return text
                return async_tool_fn

            st = StructuredTool.from_function(
                coroutine=make_tool_fn(name),
                name=name,
                description=desc
            )
            langchain_tools.append(st)

        return langchain_tools

    async def run(self, tool_host, user_text: str, sid: str, emit: Callable[[dict], Any]):
        """Run turn using LangGraph Deep Agent Graph with persistent MemorySaver checkpointing."""
        from skills import get_skill, match_skill
        
        turn_prompt = self.system_prompt
        matched = match_skill(user_text)
        if matched:
            body, _ = get_skill(matched)
            if body:
                turn_prompt += f"\n\n--- ACTIVE SKILL: {matched} ---\nFollow this procedure step by step:\n" + body
                await emit({"type": "tool_start", "name": "use_skill", "args": {"name": matched}})
                await emit({"type": "tool_result", "name": "use_skill", "ok": True, "text": f"Loaded skill '{matched}'."})

        llm = create_langchain_model()
        tools = self._convert_tools(tool_host, emit)

        graph = create_react_agent(
            model=llm,
            tools=tools,
            prompt=turn_prompt,
            checkpointer=self.checkpointer
        )

        thread_config = {"configurable": {"thread_id": str(sid)}}
        await emit({"type": "status", "text": "thinking…"})

        accumulated_text = ""

        # Stream graph execution events live to WebSocket UI
        async for event in graph.astream_events({"messages": [("user", user_text)]}, thread_config, version="v2"):
            kind = event.get("event")
            if kind == "on_chat_model_stream":
                chunk = event.get("data", {}).get("chunk", {})
                raw_content = getattr(chunk, "content", "")
                delta = _extract_chunk_text(raw_content)
                if delta:
                    accumulated_text += delta
                    await emit({"type": "token", "text": delta})

        # Reliable assistant emit for C++ AssistantPanel rendering
        if accumulated_text.strip():
            await emit({"type": "assistant", "text": accumulated_text.strip()})


# ----------------------------------------------------------------------
# Main Agent Facade
# ----------------------------------------------------------------------
class Agent:
    def __init__(self):
        if HAS_LANGCHAIN:
            self._langchain_agent = LangChainDeepAgent()
        else:
            self._langchain_agent = None

    def rebuild_backend(self):
        if HAS_LANGCHAIN:
            self._langchain_agent = LangChainDeepAgent()

    async def run(self, tool_host, user_text: str, sid: str, emit: Callable[[dict], Any]):
        if HAS_LANGCHAIN and self._langchain_agent:
            try:
                await self._langchain_agent.run(tool_host, user_text, sid, emit)
                return
            except Exception as exc:
                logger.warning(f"LangGraph execution exception: {exc}, falling back to native ReAct.")

        # Full Multi-Round Native ReAct Fallback Engine
        from llm import make_backend
        backend = make_backend(config)
        from skills import SKILL_TOOLS, get_skill, read_skill_file, match_skill
        
        history = []
        tools = tool_host.definitions() + SKILL_TOOLS
        system_prompt = _system_prompt_base()
        
        matched = match_skill(user_text)
        if matched:
            body, _ = get_skill(matched)
            if body:
                system_prompt += f"\n\n--- ACTIVE SKILL: {matched} ---\n" + body
                await emit({"type": "tool_start", "name": "use_skill", "args": {"name": matched}})
                await emit({"type": "tool_result", "name": "use_skill", "ok": True, "text": f"Loaded skill '{matched}'."})

        history.append({"role": "user", "content": user_text})
        loop = asyncio.get_running_loop()

        for _round in range(config.MAX_ROUNDS):
            await emit({"type": "status", "text": "thinking…"})
            
            def cb(delta):
                asyncio.run_coroutine_threadsafe(emit({"type": "token", "text": delta}), loop)

            res = await asyncio.to_thread(backend.run_turn, system_prompt, history, tools, cb)
            history.append({"role": "assistant", "content": res.assistant_content})

            # Surface text to C++ panel
            final_text = "".join(b.get("text", "") for b in res.assistant_content if b.get("type") == "text")
            if final_text.strip():
                await emit({"type": "assistant", "text": final_text.strip()})

            if res.stop_reason != "tool_use" or not res.tool_calls:
                return

            tool_results = []
            for call in res.tool_calls:
                call_id = call["id"]
                tname = call["name"]
                args = dict(call.get("input") or {})
                await emit({"type": "tool_start", "name": tname, "args": args})

                if tname == "use_skill":
                    body, err = get_skill(args.get("name", ""))
                    obs = {"ok": err is None, "message": body if body else err}
                elif tname == "read_skill_file":
                    content, err = read_skill_file(args.get("name", ""), args.get("file", ""))
                    obs = {"ok": err is None, "message": content if content else err}
                else:
                    # Fix: tool_host.execute takes exactly 2 arguments (name, args)
                    obs = await tool_host.execute(tname, args)

                is_err = obs.get("ok") is False or bool(obs.get("error"))
                otext = obs.get("text") or obs.get("message") or ("Success" if not is_err else "Error")
                await emit({"type": "tool_result", "name": tname, "ok": not is_err, "text": otext})

                tool_results.append({
                    "type": "tool_result",
                    "tool_use_id": call_id,
                    "content": [{"type": "text", "text": otext}],
                    "is_error": is_err
                })
            history.append({"role": "user", "content": tool_results})
