"""Ground-truth verification driver (Phases 4-5).

For each scenario:
  1. reset the headless IRISApplication (assistant_eval.exe) to the phantom
  2. apply `setup` tool calls DIRECTLY (preconditions, ground truth)
  3. record BEFORE state
  4. run the prompt through the LLM (sidecar /wsbridge); each tool_call the LLM
     makes is executed against the REAL IRISApplication and the real result is
     returned to the model
  5. record AFTER state
  6. verify expected_actions (selection) + expected_state (execution ground truth)
  7. classify PASS / FAIL / HALLUCINATION  (+ ERROR on infra failure)

Writes results.jsonl incrementally (crash-safe) and results.json at the end.

Usage: python run_eval.py [--limit N] [--ids S001,S002] [--out results.json]
"""
import sys, os, json, time, subprocess, asyncio, argparse, re
sys.stdout.reconfigure(encoding="utf-8")
from websockets.legacy.client import connect
from tool_schemas import TOOLS

HERE = os.path.dirname(os.path.abspath(__file__))
EVAL_EXE = r"D:/itksnap-build/snap-build/assistant_eval.exe"
PHANTOM = r"D:/itksnap-build/phantom.nii.gz"
SIDECAR = "ws://127.0.0.1:8077/wsbridge/eval"
NEG = ("cannot", "can't", "unable", "no tool", "don't have", "do not have",
       "not able", "couldn't", "could not", "i'm sorry", "unfortunately", "not possible")


# ---------------- headless harness wrapper ----------------
class Harness:
    def __init__(self):
        self.p = subprocess.Popen([EVAL_EXE], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)
        self._readline()  # READY
        self._send(f"load {PHANTOM}"); self._readline()

    def _send(self, s): self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def _readline(self):
        line = self.p.stdout.readline()
        return line.strip()

    def _payload(self, line):
        i = line.find("{")
        return json.loads(line[i:]) if i >= 0 else {}

    def reset(self): self._send("reset"); self._readline()
    def state(self): self._send("state"); return self._payload(self._readline())

    def tool(self, name, args):
        parts = [f"tool {name}"]
        for k, v in (args or {}).items():
            sv = str(v).replace(" ", "_")
            parts.append(f"{k}={sv}")
        self._send(" ".join(parts))
        return self._payload(self._readline())

    def close(self):
        try: self._send("quit"); self.p.wait(timeout=5)
        except Exception: self.p.kill()


# ---------------- state assertion checker ----------------
def label_by_id(state, lid):
    for l in state.get("labels", []):
        if l["id"] == int(lid): return l
    return None

def check_state(expected, after, before):
    fails = []
    if "loaded" in expected and bool(after.get("loaded")) != expected["loaded"]:
        fails.append(f"loaded!={expected['loaded']}")
    if expected.get("not_loaded") and after.get("loaded"):
        fails.append("expected not_loaded")
    for lid in expected.get("labels_present", []):
        if not label_by_id(after, lid): fails.append(f"label {lid} missing")
    for lid in expected.get("labels_absent", []):
        if label_by_id(after, lid): fails.append(f"label {lid} should be absent")
    for lid, cond in expected.get("label", {}).items():
        L = label_by_id(after, lid)
        if not L: fails.append(f"label {lid} missing"); continue
        if "voxels_gt" in cond and not (L["voxels"] > cond["voxels_gt"]): fails.append(f"L{lid} voxels {L['voxels']}<= {cond['voxels_gt']}")
        if "voxels_between" in cond:
            lo, hi = cond["voxels_between"]
            if not (lo <= L["voxels"] <= hi): fails.append(f"L{lid} voxels {L['voxels']} not in [{lo},{hi}]")
        if "imin_gte" in cond and not (L["imin"] >= cond["imin_gte"] - 1e-6): fails.append(f"L{lid} imin {L['imin']}<{cond['imin_gte']}")
        if "imax_lte" in cond and not (L["imax"] <= cond["imax_lte"] + 1e-6): fails.append(f"L{lid} imax {L['imax']}>{cond['imax_lte']}")
    if "cursor_eq" in expected and after.get("cursor") != expected["cursor_eq"]:
        fails.append(f"cursor {after.get('cursor')} != {expected['cursor_eq']}")
    if expected.get("cursor_changed") and after.get("cursor") == before.get("cursor"):
        fails.append("cursor did not change")
    if expected.get("undo_available") and not after.get("undo"):
        fails.append("undo not available")
    if "n_valid_labels_gte" in expected and after.get("n_valid_labels", 0) < expected["n_valid_labels_gte"]:
        fails.append("too few valid labels")
    if "file_written" in expected:
        pth = expected["file_written"]
        if not (os.path.exists(pth) and os.path.getsize(pth) > 0):
            fails.append(f"file not written: {pth}")
    return (len(fails) == 0), fails


def affirmative(text):
    t = (text or "").lower().strip()
    if not t: return False
    return not any(k in t for k in NEG)


def classify(expected_actions, called, verifiable, state_ok, final):
    aff = affirmative(final)
    called_set = set(called)
    if not expected_actions:  # adversarial: should call NO tool
        if not called: return "PASS", "correctly refused, no tool"
        return ("HALLUCINATION" if aff else "FAIL"), f"called {called} for an unavailable capability"
    selection_ok = set(expected_actions).issubset(called_set)
    failed = (not selection_ok) or (verifiable and not state_ok)
    if not failed: return "PASS", "actions+state verified" if verifiable else "actions verified"
    reason = []
    if not selection_ok: reason.append(f"missing {set(expected_actions)-called_set}")
    if verifiable and not state_ok: reason.append("state mismatch")
    return ("HALLUCINATION" if aff else "FAIL"), "; ".join(reason)


# ---------------- one scenario ----------------
async def run_scenario(sc, H):
    # setup + before state
    H.reset()
    for step in sc.get("setup", []):
        H.tool(step["tool"], step.get("args", {}))
    # clean any expected output files so file_written is a real check
    fw = sc.get("expected_state", {}).get("file_written")
    if fw and os.path.exists(fw):
        try: os.remove(fw)
        except Exception: pass
    before = H.state()

    called, final = [], ""
    t0 = time.time()
    try:
        async with connect(SIDECAR, max_size=None, open_timeout=15) as ws:
            await asyncio.wait_for(ws.recv(), 15)                     # ready
            await ws.send(json.dumps({"type": "hello", "tools": TOOLS}))
            await asyncio.wait_for(ws.recv(), 15)                     # hello_ack
            await ws.send(json.dumps({"type": "user", "text": sc["prompt"]}))
            while time.time() - t0 < 200:
                e = json.loads(await asyncio.wait_for(ws.recv(), 200))
                ty = e.get("type")
                if ty == "tool_call":
                    name, args = e["name"], e.get("args", {})
                    called.append(name)
                    res = H.tool(name, args)          # execute against REAL IRISApplication
                    await ws.send(json.dumps({"type": "tool_result", "id": e["id"],
                                              "ok": bool(res.get("ok", True)),
                                              "text": res.get("text", "")}))
                elif ty == "assistant":
                    final = e["text"]
                elif ty == "turn_end":
                    break
    except Exception as ex:
        after = H.state()
        return {"id": sc["id"], "category": sc["category"], "difficulty": sc["difficulty"],
                "prompt": sc["prompt"], "classification": "ERROR", "reason": f"infra: {type(ex).__name__}: {ex}",
                "called": called, "latency_s": round(time.time() - t0, 1),
                "expected_actions": sc["expected_actions"]}
    latency = round(time.time() - t0, 1)
    after = H.state()
    state_ok, fails = (True, [])
    if sc.get("verifiable"):
        state_ok, fails = check_state(sc.get("expected_state", {}), after, before)
    klass, reason = classify(sc["expected_actions"], called, sc.get("verifiable", False), state_ok, final)
    if fails: reason = (reason + " | " + "; ".join(fails)).strip(" |")
    return {"id": sc["id"], "category": sc["category"], "difficulty": sc["difficulty"],
            "prompt": sc["prompt"], "classification": klass, "reason": reason,
            "called": called, "expected_actions": sc["expected_actions"],
            "verifiable": sc.get("verifiable", False), "state_ok": state_ok,
            "final": (final or "")[:200], "latency_s": latency,
            "before_labels": [l["id"] for l in before.get("labels", [])],
            "after_labels": [l["id"] for l in after.get("labels", [])]}


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--ids", default="")
    ap.add_argument("--out", default="results.json")
    ap.add_argument("--jsonl", default="results.jsonl")
    a = ap.parse_args()

    data = json.load(open(os.path.join(HERE, "test_scenarios.json")))
    scenarios = data["scenarios"]
    if a.ids:
        want = set(a.ids.split(",")); scenarios = [s for s in scenarios if s["id"] in want]
    if a.limit: scenarios = scenarios[:a.limit]

    H = Harness()
    results = []
    jl = open(os.path.join(HERE, a.jsonl), "w", encoding="utf-8")
    t_start = time.time()
    for i, sc in enumerate(scenarios, 1):
        r = await run_scenario(sc, H)
        results.append(r); jl.write(json.dumps(r) + "\n"); jl.flush()
        print(f"[{i}/{len(scenarios)}] {r['id']} {r['classification']:13s} d{r['difficulty']} "
              f"{r['latency_s']:5.1f}s  {sc['prompt'][:52]!r}")
        sys.stdout.flush()
    jl.close(); H.close()
    json.dump({"count": len(results), "elapsed_s": round(time.time() - t_start, 1),
               "results": results}, open(os.path.join(HERE, a.out), "w"), indent=1)
    from collections import Counter
    c = Counter(r["classification"] for r in results)
    print("\n=== DONE ===", dict(c), f"in {round(time.time()-t_start,1)}s")

asyncio.run(main())
