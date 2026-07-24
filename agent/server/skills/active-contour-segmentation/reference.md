# Active-Contour Reference — intensity bands, seed/iteration tuning, troubleshooting

Deep detail for the `active-contour-segmentation` skill. The `active_contour_segment`
tool exposes exactly these knobs: `lower`, `upper` (intensity band), `seed_x/y/z`,
`seed_radius_mm`, `iterations`, `label`. Evolution forces (balloon / curvature /
advection) are managed internally by the tool — you tune the result through the band,
the seed, the radius, and the iteration count. This file explains how.

## 1. How the band `[lower, upper]` maps to ITK-SNAP's "speed image"
Internally the tool thresholds the image into a signed speed image: voxels inside
`[lower, upper]` are "inside" (the snake expands there), voxels outside are
"background" (the snake contracts there). The developer rule of thumb (Yushkevich,
itksnap-users forum): pick the band so the **target is solidly "inside" and the
surrounding tissue is solidly "background"** — i.e. the tightest band that still
fully covers the target. A band that also covers adjacent tissue creates an
intensity "bridge" the snake will leak across.

## 2. Starter intensity bands by modality / structure
Always read the actual range from `get_scene_overview` first; these are starting
points, not absolutes.

| Image / structure | Band `[lower, upper]` | Notes / source |
|---|---|---|
| **CT bone** | ~[300, 3000] HU | bone is far above soft tissue; snake rarely needed (threshold works) |
| **CT soft-tissue lesion / organ** | center on the lesion's HU ±~40–80 | e.g. liver ~[40,120] HU; tune from overview |
| **CT contrast-enhanced vessel/tumor core** | ~[100, 400] HU | enhancing tissue is bright |
| **CT cardiac blood pool (tutorial)** | [226, 518] | ComputationalPhysiology guide |
| **CT generic (RSNA exercise)** | [67, 237], ~350 iterations | RSNA 2017 handout |
| **MR bright lesion (T2/FLAIR hyperintensity)** | upper ~60–70% of range → max | lesion brighter than normal tissue |
| **MR structure, mid-intensity (e.g. caudate)** | narrow two-sided band, e.g. [48, 63] | official Section 6; smoothness handled internally |
| **MR stroke lesion (T1)** | band so lesion=white, normal=blue | neuroimaging-core lesion guide |

Rule: **bright target** → set `lower` high, `upper` = image max. **Dark target** →
set `lower` = image min, `upper` low. **Mid-intensity target** → a two-sided band
bracketing its intensity.

## 3. Seed radius (`seed_radius_mm`)
- Default **3 mm**. Bubbles smaller than ~2–3 voxels can shrink and vanish before
  they grab the speed image — do not go below ~2 mm.
- Small/thin structure (small nodule, vessel) → **2–3 mm**.
- Large compact structure (big tumor, organ) → **4–6 mm** so evolution starts
  nearer the boundary and converges faster.
- Prefer **one larger bubble** over several tiny ones when a single seed suffices.

## 4. Iterations (`iterations`)
- Default **40** evolution steps. This is where you trade fill vs. leak.
- Small structure → 40 is often enough.
- Large/elongated structure → **60–150**. Real GUI lesion runs used hundreds of
  raw steps (~950 for a big stroke lesion); scale up if under-filled.
- **Stop as soon as it fills.** Leakage is progressive — the extra iterations
  after the structure is full are exactly when it floods neighbours. If a run
  leaked, re-run with fewer iterations and/or a tighter band.

## 5. Troubleshooting table

| Symptom | Cause | Fix |
|---|---|---|
| Result volume ≈ 0 | Seed voxel not inside `[lower, upper]`; snake never grew | Re-check the band covers the seed's intensity (`get_cursor_info` at the seed); move seed into the target; widen band slightly |
| Volume far too large / spilled into neighbours | Band too wide (intensity bridge) or too many iterations | `undo`; tighten band toward target core; lower `iterations`; re-run |
| Thin / patchy / stops short | Band too narrow or too few iterations | Widen band a little; raise `iterations` (e.g. 40 → 100) |
| Disconnected structure only partly captured | Single seed can't reach all parts | Run `active_contour_segment` once per part, same `label`, different seed |
| Ragged / staircased surface | Normal for a raw snake | `smooth_labels` on the result, `sigma_mm` 0.5–1.0 |
| Bilateral structure merged into one blob | One band grew both sides through a bridge | Seed and grow each side separately into distinct labels; or tighten band |

## 6. QC heuristics from real practice
- **Symmetry check:** for paired anatomy (caudates, hippocampi, hemispheric
  lesions), compare the grown region to the contralateral side. Sulci or normal
  tissue wrongly included = false positives.
- **Volume sanity:** compare the reported mL to the plausible physiological size of
  the structure. An order-of-magnitude miss means a leak or a mis-seed.
- **Always** `measure_volume` + `focus_label` after the run — never report a snake
  result you haven't measured.

## 7. Snake vs. threshold — quick decision
- Only object in that intensity band, anywhere in the image → `threshold_segment`.
- One specific object among same-intensity neighbours, or want a smooth connected
  region → `active_contour_segment` (this skill).

Sources: official ITK-SNAP tutorials (Sections 5–7), the itksnap-users forum
(Yushkevich/Cook on region-competition force and bubble rules), the neuroimaging-core
stroke-lesion walkthrough, and the ComputationalPhysiology mesh guide.
