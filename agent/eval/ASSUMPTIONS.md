# ASSUMPTIONS LOG

Autonomous run started 2026-07-17. Every non-obvious decision made without asking
the user is recorded here with rationale.

---

## A1 — Target platform: ITK-SNAP (not OHIF/Cornerstone3D)  [CRITICAL]
**Ambiguity:** The task spec is written entirely in OHIF Viewer / Cornerstone3D
terms — `CommandsManager`, `DisplaySetService`, `MeasurementService`,
`SegmentationService`, `ViewportGridService`, `HangingProtocolService`,
`Cornerstone3D`, `DICOMweb`, hanging protocols, MPR. None of these exist in our
project. The entire session (dozens of turns) has built an **LLM agent embedded
in ITK-SNAP** (native Qt dock → local sidecar → llama.cpp `localmodel`), with ~33
tools wired against ITK-SNAP's C++ API.

**Decision:** Apply the 5-phase methodology to the **ITK-SNAP agent we built**,
mapping OHIF concepts to ITK-SNAP equivalents (see RESEARCH.md mapping table).
**Rationale:**
- "Run *this* task" in the context of an ongoing ITK-SNAP project → adapt, don't restart.
- Standing up an OHIF/Cornerstone3D project from scratch is not achievable well
  in an autonomous run and would discard all working ITK-SNAP integration.
- The rigor (research → refined tools → 100+ scenarios → ground-truth state
  verification → report) is platform-agnostic and transfers cleanly.
**Reversibility:** If OHIF was actually intended, the deliverables (methodology,
scenario schema, verification design, report structure) all port over; only the
API layer changes.

## A2 — Ground-truth = real IRISApplication state, verified headlessly
**Ambiguity:** Phase 4 wants real viewer-state verification, but the GUI dock
cannot be driven programmatically (no click automation available in this env).
**Decision:** Build a **headless C++ evaluation harness** that instantiates a real
`IRISApplication` (ITK-SNAP's Qt-free Logic library — the same one the GUI uses),
loads a synthetic phantom with known intensities, executes the tool operations
through the shared tool implementation, and reads back actual state (label voxel
counts, per-label intensity ranges, cursor, undo availability, etc.).
**Rationale:** IRISApplication runs headless (proven by ITK-SNAP's own
`logic_api_test`). This gives genuine ground truth for the TOOL layer without a GUI.
**Consequence:** Tools that require GUI-only sub-models (layout, 3D mesh render)
are verified for "call succeeded / state flag set" rather than pixel output, and
flagged as such per scenario.

## A3 — Two-axis verification (selection vs. execution)
**Decision:** Separate and measure two things:
1. **Tool SELECTION** accuracy — does the LLM pick + parameterize the right tools
   from a prompt? (LLM-in-the-loop harness over the `/wsbridge` protocol.)
2. **Tool EXECUTION** correctness — do the tools actually produce the claimed
   real state? (headless IRISApplication ground truth.)
**Hallucination** = SELECTION says success / EXECUTION shows no matching state change.
**Rationale:** Conflating them hides where failures come from; the task's
hallucination definition ("claimed success, no real state change") requires both.

## A4 — Synthetic phantom as the standard test image
**Decision:** Use a generated 128³ (and a 256³) phantom with known regions
(background=40, organ=100, nodule=220, plus noise) as the canonical test volume,
so expected intensity thresholds and voxel counts are deterministic and checkable.
**Rationale:** No dependency on private patient data; exact ground truth is known.

## A5 — Scenario scale and difficulty
**Decision:** ≥100 scenarios. Per-tool: 5 real-world prompts × ~33 tools. Composite:
multi-tool prompts scored 1–10; 7–10 require ≥3 sequential tool calls. Store in
`test_scenarios.json` with {id, prompt, difficulty, category, expected_actions,
expected_state}.

## A6 — Model + latency caveat
**Decision:** All LLM runs use the user's `localmodel` (a very large model on
llama.cpp at localhost:11440), sequentially, to avoid overloading it (observed:
concurrent load → timeouts). Latency figures are for that model/host and are not
portable benchmarks.

<!-- New assumptions appended below as the run proceeds. -->
