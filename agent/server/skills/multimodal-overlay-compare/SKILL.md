---
name: multimodal-overlay-compare
description: >
  Load and compare multiple images of the same subject — different modalities
  (T1/T2/FLAIR, CT+MR) or timepoints (baseline vs follow-up) — as overlaid layers, and
  compare structures/volumes across them. Use when the user wants to load an overlay,
  compare two scans, look at another modality alongside the main image, check change
  over time, or view baseline vs follow-up together.
---

# Multi-Modal / Longitudinal Overlay Comparison

ITK-SNAP shows a main image plus additional images as **layers yoked by physical
(world) coordinates** — if the images share correct headers, they line up
automatically with no registration. This skill covers loading, viewing, and comparing
them with the current tool set. Read the alignment note before promising a comparison.

## Alignment reality check (read first)
- The current tool set can **load** a main image and overlays and **display/compare**
  them, but it has **no registration tool**. Overlays line up ONLY if they already
  share the same physical space (same-session scans, or pre-registered externally).
- If two scans were acquired on different days or different scanners and were not
  registered, they will appear **misaligned**, and that is not something these tools
  can fix. When that happens, say so and tell the user to register them first
  (ITK-SNAP's own `Tools → Registration`, or ANTs/Greedy/FSL), then reload — don't
  pretend the overlay is aligned when it isn't.

## Procedure

1. **Load the reference (main) image.** `load_image(path)` — this is the fixed
   reference everything else overlays onto (e.g. the baseline scan, or the T1).
   `get_scene_overview` to confirm dimensions and intensity range.

2. **Load the other image(s) as overlays.** `load_overlay(path)` for each additional
   modality/timepoint (T2, FLAIR, follow-up scan).

3. **Check they actually line up.** `set_layout("all")`, then verify anatomy
   coincides across the layers (e.g. `move_cursor` to a clear landmark and confirm it
   sits on the same structure in the overlay). If they're grossly misaligned, stop and
   raise the registration issue from the note above.

4. **Blend for visual comparison.** Use opacity to flick between / blend the layers:
   - `set_segmentation_opacity` and per-layer opacity to fade the overlay in and out,
     which is the standard ITK-SNAP way to compare alignment and spot change
     (bright↔dark, grew↔shrank) at a glance.
   - Centre on the region of interest with `focus_label` (if a structure is labelled)
     or `move_cursor`.

5. **Compare quantitatively (longitudinal).** For baseline-vs-follow-up volume change:
   - Segment the structure on each timepoint (load one as main, segment, measure;
     repeat) or load an existing segmentation for each with `load_segmentation`.
   - `measure_volume` / `measure_all_labels` on each, and report the **absolute and
     percent change** in mL.
   - Save each timepoint's stats with `save_statistics` using timepoint-tagged names
     (see the lesion-volumetry-report skill for report conventions).

6. **Tidy up.** `unload_overlays` to return to just the main image when done.

## Modality-pairing notes
- **Same modality, two timepoints** (baseline/follow-up) — the cleanest comparison;
  volume change is directly meaningful if both are segmented the same way.
- **Different modalities** (T1 vs FLAIR, CT vs MR) — useful for seeing a lesion that's
  bright on one and dark on another; compare visually, and segment on whichever
  modality shows the structure best.

## Pitfalls
- **Assuming overlays are aligned.** Different-day/scanner scans usually aren't;
  verify at a landmark, and hand off to external registration if not.
- **Comparing volumes segmented by different methods.** For a fair longitudinal
  comparison, segment both timepoints the same way (same band/snake settings).
- **Leaving overlays loaded.** `unload_overlays` when finished so later single-image
  work isn't confused by stray layers.
