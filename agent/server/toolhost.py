"""Tool hosting: WHERE the agent's tools actually run.

The agent loop doesn't care who executes a tool — it just needs
`definitions()` (the schemas to send the model) and `execute(name, args)`
(run it, return an observation dict). Two hosts implement that:

  LocalToolHost  — runs tools in THIS process over SimpleITK (the standalone
                   web app / testing).
  RemoteToolHost — DELEGATES execution to the connected client. This is the
                   embedding protocol: inside ITK-SNAP, the C++ AssistantPanel
                   is the client — it receives a {type:"tool_call"} over the
                   websocket, runs it against IRISApplication, and sends back a
                   {type:"tool_result"}. The agent brain is unchanged; only the
                   executor moves into ITK-SNAP.

Observation dict shape the agent expects: {"ok": bool, "message": str,
optionally "error": str, and "viewer"/"focus" for the local viewer}.
"""
from __future__ import annotations

import asyncio


class LocalToolHost:
    """Execute tools in-process over SimpleITK (standalone testing)."""

    def __init__(self, session):
        self.session = session

    def definitions(self):
        from tools import tool_definitions
        return tool_definitions()

    async def execute(self, name, args):
        from tools import run_tool, ToolError
        try:
            return await asyncio.to_thread(run_tool, self.session, name, args)
        except ToolError as e:
            return {"ok": False, "error": str(e)}
        except Exception as e:  # noqa: BLE001
            return {"ok": False, "error": f"{type(e).__name__}: {e}"}


class RemoteToolHost:
    """Delegate tool execution to the connected client (e.g. ITK-SNAP).

    The client declares its tools once (hello), then executes each tool_call
    the agent issues and returns a tool_result correlated by id.
    """

    def __init__(self, send, timeout=180):
        self._send = send                 # async callable: send a json message to the client
        self._defs = []                   # tool schemas provided by the client
        self._pending: dict[str, asyncio.Future] = {}
        self._counter = 0
        self._timeout = timeout

    # -- called from the websocket reader --------------------------------
    def set_tools(self, defs):
        self._defs = defs or []

    def resolve(self, call_id, result):
        """Client returned a tool_result — hand it to the waiting execute()."""
        fut = self._pending.pop(call_id, None)
        if fut and not fut.done():
            fut.set_result(result)

    def fail_all(self, reason):
        for fut in self._pending.values():
            if not fut.done():
                fut.set_result({"ok": False, "text": reason})
        self._pending.clear()

    # -- agent-facing interface ------------------------------------------
    def definitions(self):
        return self._defs

    async def execute(self, name, args):
        self._counter += 1
        call_id = f"rt_{self._counter}"
        fut = asyncio.get_running_loop().create_future()
        self._pending[call_id] = fut
        await self._send({"type": "tool_call", "id": call_id, "name": name, "args": args})
        try:
            result = await asyncio.wait_for(fut, timeout=self._timeout)
        except asyncio.TimeoutError:
            self._pending.pop(call_id, None)
            return {"ok": False, "error": f"Tool '{name}' timed out (no result from client/ITK-SNAP)."}
        # normalize the client's {ok, text} into the agent's observation dict
        if result.get("ok", True):
            return {"ok": True, "message": result.get("text", "done")}
        return {"ok": False, "error": result.get("text") or result.get("error") or "tool failed"}
