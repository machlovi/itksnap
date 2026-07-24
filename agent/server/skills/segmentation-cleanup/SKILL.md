---
name: segmentation-cleanup
description: >
  Clean up and refine an existing segmentation — smooth jagged/staircased boundaries,
  fill gaps between slices that were only labelled sparsely, remove stray islands or
  leaked regions, and re-measure the corrected volume. Use when the user asks to
  smooth, clean, refine, fix, polish, tidy, de-stair, fill in, or interpolate a mask,
  or after a threshold/snake result came out rough or leaked.
---

# Segmentation Cleanup & Refinement

ITK-SNAP is a segmentation **editor**, and its native cleanup is a small, sharp
toolkit: **label smoothing** (Gaussian), **between-slice interpolation**, and
**label arithmetic** (replace/clear). Know what it can and cannot do so you don't
promise morphology it doesn't have (see the honesty note at the end).

## First, know what you're fixing
`get_scene_overview` and `measure_volume` on the target label to record the starting
volume. `focus_label` to see the problem. Classify it:
- **Ragged / staircased boundary** → smoothing (step A).
- **Only some slices labelled, gaps between them** → interpolation (step B).
- **Stray islands / a leaked blob / wrong-label voxels** → label arithmetic (step C).
Often you'll do more than one, in the order A/B/C that fits.

## A. Smooth a jagged boundary — `smooth_labels`
- `smooth_labels(label, sigma_mm)` applies a per-label Gaussian that rounds off
  staircase artifacts and 1–2 voxel spurs without letting one label eat another.
- **Sigma guidance:** start `sigma_mm` ≈ **0.5–1.0**. Small structures / fine detail →
  0.5. Large blobby structures → 1.0–2.0. Bigger sigma = smoother but erodes fine
  features and shifts volume more.
- Smoothing changes the volume slightly — **`measure_volume` again afterwards** and
  report the before/after so the number stays honest.
- It will NOT remove a large disconnected island or fill a large cavity — that's
  step C or external morphology.

## B. Fill gaps between sparsely-labelled slices — `interpolate_labels`
- When the user labelled, say, every 5th slice, `interpolate_labels(label)` fills the
  slices in between using morphological contour interpolation (it aligns adjacent
  labelled shapes and interpolates the transition). Omit the label to interpolate all.
- **Prerequisite:** there must be shape overlap between consecutive labelled slices.
  Big gaps or a twisty shape produce artifacts — interpolation needs slices close
  enough that the structure overlaps from one to the next.
- After interpolating, `measure_volume` and `focus_label` to confirm the fill is
  anatomically sensible (scroll-equivalent: check a few slices via `move_cursor`).

## C. Remove islands / leaks / relabel — label arithmetic
- **Wrong-label region → background:** `clear_label(label)` erases one label entirely
  (use when a whole structure is bad and you'll redo it). To erase only part, that
  part must be its own label first.
- **Merge or reassign:** `replace_label(from_label, to_label)` reassigns every voxel of
  one label to another (`to_label=0` clears). Use to merge an accidental second label
  into the right one, or to move a mislabelled region.
- **Quantify the fix:** `count_voxels` before and after so you can state how much was
  removed/moved.
- `undo` / `redo` reverse the last edit — use `undo` immediately if a cleanup step made
  it worse, then try different parameters.

## D. Re-measure and report
Always end with `measure_volume` on the cleaned label and report the **before → after**
volume and what you changed (smoothed with sigma X, interpolated N slices, removed an
island of K voxels). If the user wants it kept, `save_workspace` / `save_statistics`.

## Honest capability note (do not over-promise)
ITK-SNAP has **no native binary morphology** — no dilate/erode/open/close, no
"fill all holes", no "keep largest connected component" button, and the current tool
set exposes none either. If the user needs those:
- Approximate **open/close** by combining `smooth_labels` (removes small spurs/holes)
  with a re-threshold — but say it's an approximation.
- For true dilate/erode/holefill/keep-largest, tell the user those are done in
  **Convert3D (`c3d`)** (same author: `-dilate`, `-erode`, `-holefill`, `-comp`) or
  3D Slicer's Segment Editor, then reloaded via `load_segmentation`. Don't claim the
  in-app tools did it.

## Pitfalls
- **Over-smoothing** (sigma too large) melts real detail and moves volume — start
  small, re-measure.
- **Interpolating across too large a gap or the wrong axis** → artifacts; the source
  slices must overlap.
- **Reporting the pre-cleanup volume** → always re-measure after editing.
- **Claiming morphology that isn't there** → be explicit about the c3d/Slicer handoff.
