"""Generate TEST_REPORT.md + normalized results.json from results.jsonl (Phase 5).

Reads the incremental results.jsonl (works on a partial run too) and produces:
- overall pass/fail/hallucination/error counts + rates
- per-tool accuracy (the 5 tests per tool)
- composite accuracy by difficulty tier
- hallucination rate
- latency overall + per difficulty tier
- root-cause listing for every non-PASS case
- recommendations
"""
import json, os, statistics
from collections import defaultdict, Counter

HERE = os.path.dirname(os.path.abspath(__file__))
rows = [json.loads(l) for l in open(os.path.join(HERE, "results.jsonl"), encoding="utf-8") if l.strip()]

N = len(rows)
klass = Counter(r["classification"] for r in rows)
npass = klass.get("PASS", 0); nhall = klass.get("HALLUCINATION", 0)
nfail = klass.get("FAIL", 0); nerr = klass.get("ERROR", 0)
def pct(a, b): return f"{100.0*a/b:.1f}%" if b else "n/a"

lat = [r["latency_s"] for r in rows if "latency_s" in r]
def tier(d): return "1-3 (simple)" if d <= 3 else "4-6 (moderate)" if d <= 6 else "7-10 (hard/multi-step)"

# per-tool (category tool:X)
per_tool = defaultdict(list)
for r in rows:
    if r["category"].startswith("tool:"):
        per_tool[r["category"][5:]].append(r)

# composite/adversarial by difficulty tier
by_tier = defaultdict(list)
for r in rows:
    by_tier[tier(r["difficulty"])].append(r)

# latency by tier
lat_tier = defaultdict(list)
for r in rows:
    if "latency_s" in r: lat_tier[tier(r["difficulty"])].append(r["latency_s"])

out = []
W = out.append
W("# TEST REPORT — ITK-SNAP LLM Agent (Ground-Truth Verification)\n")
W("> Every scenario was executed against a **real headless `IRISApplication`**; "
  "PASS/FAIL/HALLUCINATION is decided by reading the actual segmentation state "
  "(per-label voxel counts + intensity ranges, cursor, files on disk), never the "
  "model's text. See ASSUMPTIONS.md and RESEARCH.md for scope.\n")

W("## 1. Summary\n")
W(f"| Metric | Value |")
W(f"|---|---|")
W(f"| Total scenarios run | {N} |")
W(f"| PASS | {npass} ({pct(npass,N)}) |")
W(f"| FAIL | {nfail} ({pct(nfail,N)}) |")
W(f"| **HALLUCINATION** (claimed success, wrong/absent state) | {nhall} ({pct(nhall,N)}) |")
W(f"| ERROR (infra/timeout) | {nerr} ({pct(nerr,N)}) |")
W(f"| Verifiable scenarios (ground-truth state checked) | {sum(1 for r in rows if r.get('verifiable'))} |")
W(f"| Overall accuracy (PASS / total) | **{pct(npass,N)}** |")
W(f"| Hallucination rate | **{pct(nhall,N)}** |")
if lat:
    W(f"| Latency mean / median / p90 (s) | {statistics.mean(lat):.1f} / {statistics.median(lat):.1f} / "
      f"{sorted(lat)[int(0.9*len(lat))-1]:.1f} |")
W("")

W("## 2. Per-tool accuracy (5 tests each)\n")
W("| Tool | n | PASS | HALLUC | FAIL | ERR | accuracy |")
W("|---|---|---|---|---|---|---|")
for tool in sorted(per_tool):
    rs = per_tool[tool]; k = Counter(r["classification"] for r in rs)
    W(f"| `{tool}` | {len(rs)} | {k.get('PASS',0)} | {k.get('HALLUCINATION',0)} | "
      f"{k.get('FAIL',0)} | {k.get('ERROR',0)} | {pct(k.get('PASS',0),len(rs))} |")
W("")

W("## 3. Composite / difficulty-tier accuracy\n")
W("| Difficulty tier | n | PASS | HALLUC | FAIL | ERR | accuracy | mean latency |")
W("|---|---|---|---|---|---|---|---|")
for t in ["1-3 (simple)", "4-6 (moderate)", "7-10 (hard/multi-step)"]:
    rs = by_tier.get(t, []); k = Counter(r["classification"] for r in rs)
    ml = f"{statistics.mean(lat_tier[t]):.1f}s" if lat_tier.get(t) else "-"
    W(f"| {t} | {len(rs)} | {k.get('PASS',0)} | {k.get('HALLUCINATION',0)} | "
      f"{k.get('FAIL',0)} | {k.get('ERROR',0)} | {pct(k.get('PASS',0),len(rs))} | {ml} |")
W("")
# per exact difficulty
W("Per exact difficulty score:\n")
W("| difficulty | n | PASS | accuracy |")
W("|---|---|---|---|")
bd = defaultdict(list)
for r in rows: bd[r["difficulty"]].append(r)
for d in sorted(bd):
    rs = bd[d]; p = sum(1 for r in rs if r["classification"] == "PASS")
    W(f"| {d} | {len(rs)} | {p} | {pct(p,len(rs))} |")
W("")

W("## 4. Latency\n")
if lat:
    W(f"- Overall: mean **{statistics.mean(lat):.1f}s**, median {statistics.median(lat):.1f}s, "
      f"min {min(lat):.1f}s, max {max(lat):.1f}s (model = user's `localmodel` on llama.cpp, sequential).")
    for t in ["1-3 (simple)", "4-6 (moderate)", "7-10 (hard/multi-step)"]:
        if lat_tier.get(t):
            W(f"- {t}: mean {statistics.mean(lat_tier[t]):.1f}s over {len(lat_tier[t])} scenarios.")
W("")

W("## 5. Failure & hallucination root-cause analysis\n")
bad = [r for r in rows if r["classification"] != "PASS"]
if not bad:
    W("_No non-PASS cases._\n")
else:
    W(f"{len(bad)} non-PASS case(s):\n")
    W("| id | class | d | prompt | tools called | expected | reason |")
    W("|---|---|---|---|---|---|---|")
    for r in bad:
        W(f"| {r['id']} | {r['classification']} | {r['difficulty']} | {r['prompt'][:44]} | "
          f"{','.join(r.get('called',[])) or '(none)'} | {','.join(r.get('expected_actions',[])) or '(none)'} | "
          f"{(r.get('reason','') or '')[:70]} |")
W("")

# root-cause buckets
W("### Root-cause buckets\n")
buckets = Counter()
for r in bad:
    rc = (r.get("reason","") or "").lower()
    if r["classification"] == "ERROR": buckets["infra/timeout"] += 1
    elif "missing" in rc and "state" not in rc: buckets["wrong/incomplete tool selection"] += 1
    elif "state mismatch" in rc or "voxels" in rc or "imin" in rc or "cursor" in rc or "file not written" in rc:
        buckets["tool selected but state wrong"] += 1
    elif "unavailable capability" in rc: buckets["adversarial: invented a capability"] += 1
    else: buckets["other"] += 1
for b, c in buckets.most_common():
    W(f"- {b}: {c}")
W("")

W("## 6. Recommendations\n")
recs = []
if nhall: recs.append(f"**Hallucinations ({nhall}):** the classifier flags cases where the model's "
                      "reply was affirmative but real state did not match. Prioritise sharpening the "
                      "descriptions of the tools implicated in Section 5, and add a post-action "
                      "self-check step ('re-read the request; verify each part happened').")
if buckets.get("wrong/incomplete tool selection"):
    recs.append("**Selection gaps:** several multi-part prompts dropped a step. Reinforce the "
                "compound-completion rule in the system prompt and consider a per-clause checklist.")
if buckets.get("tool selected but state wrong"):
    recs.append("**Parameterisation:** right tool, wrong args (e.g. intensity range that captured "
                "nothing). Enrich tool results with the actual outcome (voxel count) so the model can "
                "notice a 0-voxel segmentation and retry — Phase-2 result objects already surface this.")
if nerr: recs.append(f"**Infra ({nerr} ERROR):** timeouts against the large local model; a smaller/faster "
                     "tool-calling model or higher concurrency budget would cut latency.")
recs.append("Add the currently selection-only tools (smooth/interpolate/layout/mesh) to a GUI-driven "
            "verification path so their state is ground-truth-checked too.")
for r in recs: W(f"- {r}")
W("")

open(os.path.join(HERE, "TEST_REPORT.md"), "w", encoding="utf-8").write("\n".join(out))
# normalized machine-readable summary
summary = {"total": N, "pass": npass, "fail": nfail, "hallucination": nhall, "error": nerr,
           "accuracy": npass/N if N else 0, "hallucination_rate": nhall/N if N else 0,
           "latency_mean_s": statistics.mean(lat) if lat else None,
           "per_tool": {t: dict(Counter(r["classification"] for r in rs)) for t, rs in per_tool.items()},
           "by_difficulty": {d: {"n": len(rs), "pass": sum(1 for r in rs if r["classification"]=="PASS")}
                             for d, rs in sorted(bd.items())}}
json.dump({"summary": summary, "results": rows}, open(os.path.join(HERE, "results.json"), "w"), indent=1)
print(f"Report written. {N} scenarios: PASS={npass} FAIL={nfail} HALLUC={nhall} ERROR={nerr}  acc={pct(npass,N)}")
