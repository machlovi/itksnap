---
name: prepare-display
description: >
  Make a structure clearly visible before working on it: auto or manual window/level
  (brightness/contrast), choose the view layout, and set segmentation/label opacity so
  anatomy shows through. Use when the user says the image is too dark/bright/washed
  out, can't see a structure, wants to adjust contrast/window/level, wants a specific
  view (axial/coronal/sagittal/3D), or wants the overlay more/less transparent. Also
  run this as the first step before thresholding, because you pick thresholds off what
  you can see.
---

# Prepare the Display (Contrast, Layout, Opacity)

You choose thresholds and judge segmentations by what's on screen, so getting the
display right is a real first step, not cosmetics. A washed-out image leads to bad
threshold choices.

## Procedure

1. **Read the scene.** `get_scene_overview` — confirm an image is loaded and note its
   intensity min/max (tells you whether the current window is sensible).

2. **Fix contrast.** Two paths:
   - **Fast default — `auto_window_level`.** Sets window/level to the image's robust
     intensity range (≈1st/99th percentile), which handles most images and is the
     right first move when the user just says "it's too dark" or "fix the contrast".
   - **Specific — `set_window_level(window, level)`.** Use when the user wants a
     particular preset or a structure that auto-contrast doesn't reveal. **Level** =
     brightness (window centre); **Window** = contrast (width). Both are in the image's
     intensity units (HU for CT). See `reference.md` for CT window/level presets
     (soft tissue, lung, bone, brain, etc.) — load it with `read_skill_file` when the
     user names a tissue or asks for a specific window.

3. **Set the layout** to what the task needs — `set_layout`:
   - `all` (four panels) for general work,
   - `axial` / `coronal` / `sagittal` to focus one plane (e.g. axial for most CT),
   - `3d` to inspect the rendered model (run `update_3d_mesh` first if there's a
     segmentation to show).

4. **Set overlay opacity so anatomy shows through.** If a segmentation exists:
   - `set_segmentation_opacity(50)` — ~50% is the practical default: you see the label
     AND the underlying image, which is exactly what you need to judge boundaries.
     0 = image only, 100 = solid colour hiding the anatomy.
   - Per-label control: `set_label_opacity(label, 0-255)` and
     `set_label_visibility(label, true/false)` to isolate or hide individual
     structures while reviewing.

5. **Centre on what matters.** `focus_label(label)` to centre the view on a structure,
   or `move_cursor(x,y,z)` to a coordinate the user cares about.

6. **Confirm.** Briefly state what you changed (auto-contrast vs a specific window,
   the layout, the opacity) so the user knows the display state.

## When this is a pre-step
If the real task is "segment structure X" and X is hard to see, run steps 1–2 (and 4)
first, THEN proceed to the segmentation skill. Picking a threshold band off a properly
windowed image is far more reliable.

## Pitfalls
- **Picking thresholds off a washed-out image** → wrong band. Window first.
- **Opacity at 100%** hides the anatomy under the label — keep ~50% while judging a
  segmentation.
- **Window vs level mix-up** — Level is brightness (centre), Window is contrast
  (width). For "brighter", lower the Level; for "more contrast", narrow the Window.
- **Asking for a 3D layout with no mesh built** — run `update_3d_mesh` before
  `set_layout("3d")` or the panel is empty.
