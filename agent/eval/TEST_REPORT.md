# TEST REPORT — ITK-SNAP LLM Agent (Ground-Truth Verification)

> Every scenario was executed against a **real headless `IRISApplication`**; PASS/FAIL/HALLUCINATION is decided by reading the actual segmentation state (per-label voxel counts + intensity ranges, cursor, files on disk), never the model's text. See ASSUMPTIONS.md and RESEARCH.md for scope.

## 1. Summary

| Metric | Value |
|---|---|
| Total scenarios run | 115 |
| PASS | 111 (96.5%) |
| FAIL | 1 (0.9%) |
| **HALLUCINATION** (claimed success, wrong/absent state) | 3 (2.6%) |
| ERROR (infra/timeout) | 0 (0.0%) |
| Verifiable scenarios (ground-truth state checked) | 90 |
| Overall accuracy (PASS / total) | **96.5%** |
| Hallucination rate | **2.6%** |
| Latency mean / median / p90 (s) | 17.4 / 8.3 / 29.6 |

## 2. Per-tool accuracy (5 tests each)

| Tool | n | PASS | HALLUC | FAIL | ERR | accuracy |
|---|---|---|---|---|---|---|
| `clear_label` | 5 | 4 | 0 | 1 | 0 | 80.0% |
| `clear_segmentation` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `count_voxels` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `focus_label` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `get_cursor_info` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `get_scene_overview` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `load_image` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `load_overlay` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `measure_all_labels` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `measure_volume` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `move_cursor` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `redo` | 5 | 4 | 1 | 0 | 0 | 80.0% |
| `rename_label` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `replace_label` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `save_annotations` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `save_labels` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `save_statistics` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `save_workspace` | 5 | 4 | 1 | 0 | 0 | 80.0% |
| `set_active_label` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `set_label_color` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `threshold_segment` | 5 | 4 | 1 | 0 | 0 | 80.0% |
| `undo` | 5 | 5 | 0 | 0 | 0 | 100.0% |
| `unload_main_image` | 5 | 5 | 0 | 0 | 0 | 100.0% |

## 3. Composite / difficulty-tier accuracy

| Difficulty tier | n | PASS | HALLUC | FAIL | ERR | accuracy | mean latency |
|---|---|---|---|---|---|---|---|
| 1-3 (simple) | 105 | 102 | 2 | 1 | 0 | 97.1% | 17.8s |
| 4-6 (moderate) | 10 | 9 | 1 | 0 | 0 | 90.0% | 13.1s |
| 7-10 (hard/multi-step) | 0 | 0 | 0 | 0 | 0 | n/a | - |

Per exact difficulty score:

| difficulty | n | PASS | accuracy |
|---|---|---|---|
| 1 | 15 | 15 | 100.0% |
| 2 | 67 | 65 | 97.0% |
| 3 | 23 | 22 | 95.7% |
| 4 | 10 | 9 | 90.0% |

## 4. Latency

- Overall: mean **17.4s**, median 8.3s, min 5.5s, max 200.0s (model = user's `localmodel` on llama.cpp, sequential).
- 1-3 (simple): mean 17.8s over 105 scenarios.
- 4-6 (moderate): mean 13.1s over 10 scenarios.

## 5. Failure & hallucination root-cause analysis

4 non-PASS case(s):

| id | class | d | prompt | tools called | expected | reason |
|---|---|---|---|---|---|---|
| S012 | HALLUCINATION | 2 | outline the bright nodule for me | get_scene_overview | threshold_segment | missing {'threshold_segment'}; state mismatch | label 1 missing; label |
| S037 | FAIL | 3 | remove the nodule segmentation but keep the  | get_scene_overview,measure_all_labels,count_voxels | clear_label | missing {'clear_label'}; state mismatch | label 1 should be absent |
| S078 | HALLUCINATION | 4 | bring that back | get_scene_overview,undo,get_scene_overview,get_scene_overview,get_scene_overview | redo | missing {'redo'}; state mismatch | label 1 missing |
| S084 | HALLUCINATION | 2 | preserve everything to a workspace D:/itksna | save_workspace | save_workspace | state mismatch | file not written: a workspace D:/itksnap-build/keep.i |

### Root-cause buckets

- tool selected but state wrong: 4

## 6. Recommendations

- **Hallucinations (3):** the classifier flags cases where the model's reply was affirmative but real state did not match. Prioritise sharpening the descriptions of the tools implicated in Section 5, and add a post-action self-check step ('re-read the request; verify each part happened').
- **Parameterisation:** right tool, wrong args (e.g. intensity range that captured nothing). Enrich tool results with the actual outcome (voxel count) so the model can notice a 0-voxel segmentation and retry — Phase-2 result objects already surface this.
- Add the currently selection-only tools (smooth/interpolate/layout/mesh) to a GUI-driven verification path so their state is ground-truth-checked too.
