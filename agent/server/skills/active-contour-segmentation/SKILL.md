---
name: active-contour-segmentation
description: >
  Segment a single compact structure (tumor, lesion, nodule, cyst, organ, vessel
  segment) with ITK-SNAP's semi-automatic active-contour "snake" / region-competition
  method — a seed grows a smooth, connected region through voxels in an intensity
  band, far cleaner than a raw threshold. Use whenever the user asks for active
  contour, snake, level-set, region growing/competition, or "grow this structure",
  or wants one clean connected object rather than every matching voxel in the volume.
  Also the right choice when a plain threshold_segment leaked into other structures.
---

# Active-Contour (Snake) Segmentation

This is ITK-SNAP's flagship semi-automatic pipeline. Unlike `threshold_segment`
(which paints *every* voxel in the intensity band, anywhere in the image), the
snake starts from a **seed inside the target** and grows a single **connected,
smooth** region, competing against the background. That connectivity + curvature
smoothing is why it beats thresholding for a specific tumor/lesion/organ.

The `active_contour_segment` tool performs the whole GUI pipeline internally
(enter SNAP mode → build the threshold speed image → seed a bubble →
initialize the level-set → evolve N steps → commit to the main segmentation).
Your job is to feed it a good **intensity band**, a good **seed location**, and
sensible **radius / iterations**, then verify and clean up.

## When to use this vs. a plain threshold
- **Use the snake** for ONE compact structure, when neighbouring tissue shares
  the intensity band (thresholding would grab it too), or when the user wants a
  smooth connected object.
- **Use `threshold_segment`** only when the target is the *only* thing in that
  intensity band (e.g. bone on CT, or a bright contrast blob on a black field),
  or the user explicitly wants "all voxels in range".

## Procedure

1. **Read the scene.** Call `get_scene_overview`. Confirm an image is loaded and
   note the reported intensity **min/max**. If nothing is loaded, ask for a path
   (or `load_image`) and stop. You cannot pick a band without knowing the range.

2. **Choose the intensity band `[lower, upper]` that isolates the target.**
   The band should make the target "light up" while the surrounding tissue stays
   out. Derive it from the overview range and the target type:
   - If the user gave HU/intensity numbers, use them.
   - Otherwise estimate from the structure's brightness relative to the reported
     range (bright lesion → upper part of range; see `reference.md` for a
     per-modality starter table and how to widen/narrow the band).
   - The band brackets the structure like a threshold would, but the snake then
     grows only the connected region from the seed — so a slightly loose band is
     usually fine.

3. **Place the seed INSIDE the target.** The seed is the crosshair unless you
   pass explicit coordinates. Pick, in order of preference:
   - The user gave a location/coordinate → `move_cursor` there (or pass
     `seed_x/seed_y/seed_z`).
   - The target is already a label from an earlier step → `focus_label` on it so
     the crosshair lands at its centre, then use the crosshair seed.
   - Neither → ask the user to click the structure, or pass explicit coordinates
     if the request implies them. **Never seed blindly in the middle of the
     volume** — a seed in the wrong tissue grows the wrong thing.

4. **Run the snake.** Call `active_contour_segment` with:
   - `lower`, `upper` — the band from step 2 (required).
   - `seed_x/seed_y/seed_z` — omit to use the crosshair from step 3, or pass
     explicitly.
   - `seed_radius_mm` — default 3. Use ~2 for small structures (small nodule,
     vessel), ~4–5 for large ones (big tumor, organ) so evolution starts closer
     to the boundary.
   - `iterations` — default 40. This is the number of evolution steps. Start at
     40; if the result under-fills the structure, re-run with more (60–120). If
     it leaked, use fewer and/or tighten the band (see step 6).
   - `label` — the target label id (default 1). Consider `rename_label` first so
     the result reads as e.g. "Lesion" not "Label 1".

5. **Verify against ground truth — do not trust it blind.**
   - `measure_volume` on the result label → get mL + voxel count. A near-zero
     volume means the seed was outside the band (snake never grew) — revisit
     steps 2–3.
   - `focus_label` on the result to centre the view so the user sees it.
   - Sanity-check the volume against the expected size of the structure.

6. **Fix the two classic failure modes.**
   - **Leakage** (snake flooded into neighbouring tissue → volume way too big):
     the band was too wide or iterations too high. `undo`, tighten `[lower,upper]`
     toward the target's core intensity, drop `iterations`, re-run. As a QC
     heuristic for brain lesions, compare the grown region to the contralateral
     (opposite) hemisphere — normal sulci wrongly included are false positives.
   - **Under-growth** (thin, patchy, or stops short → volume too small): band too
     narrow or too few iterations. Widen the band slightly and/or raise
     `iterations`, re-run. Multiple disconnected parts → seed each part with a
     separate `active_contour_segment` call into the same `label`.

7. **Clean up the boundary (optional but recommended).** Snake results can have a
   slightly ragged surface. `smooth_labels` on the result label with
   `sigma_mm` ~0.5–1.0 removes staircase/jagged edges without materially changing
   the volume. Re-`measure_volume` after smoothing so the reported number matches
   the final mask.

8. **Report.** State the final volume (mL) and voxel count, the band and seed
   used, and that the view is centred on the result. If the user asked to save,
   continue with `save_workspace` / `save_statistics` (or hand off to the
   lesion-volumetry-report skill).

## Multi-part or bilateral structures
For a structure with several disconnected pieces (e.g. left+right caudate,
multifocal lesion), run `active_contour_segment` once per piece, each with its own
seed, all writing into the **same** `label`. Then `measure_volume` once for the
combined total.

## Pitfalls (learned from real ITK-SNAP use)
- **Seed outside the band = nothing grows.** The single most common failure. If
  volume is ~0, the seed voxel's intensity wasn't inside `[lower, upper]`.
- **Band too wide = leakage** through any intensity "bridge" to adjacent tissue.
  Prefer a tighter band + more iterations over a loose band.
- **Confusing the snake with a threshold.** If the user just wants "everything
  brighter than X", that's `threshold_segment`, not this skill.
- **Not verifying.** Always `measure_volume` + `focus_label` after; a plausible-
  looking call can still have seeded the wrong tissue.

For per-modality intensity bands, radius/iteration tuning, and a leakage-vs-
undergrowth troubleshooting table, load `reference.md` with `read_skill_file`.
