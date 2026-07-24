---
name: multi-structure-segmentation
description: >
  Segment SEVERAL distinct structures/tissues in one image into separate named labels
  (e.g. tumor + edema, liver + spleen + both kidneys, grey vs white matter) and measure
  them all. Use when the user asks to segment more than one structure, label multiple
  regions, build a multi-label map, or compare two or more tissues. Handles label
  creation, per-structure segmentation, keeping structures from clobbering each other,
  and a combined volume table.
---

# Multi-Structure (Multi-Label) Segmentation

Each voxel can hold only ONE label, so multi-structure work is really about (a) giving
every structure its own named label and (b) segmenting them so they don't overwrite
each other. Do it one structure at a time, verifying as you go.

## Plan the labels first
Decide the full label list before segmenting (e.g. 1=Tumor, 2=Edema, 3=Necrosis).
Planning up front avoids renumbering later.

## Procedure

1. **Read the scene.** `get_scene_overview` — confirm the image and note the intensity
   range and any existing labels. ITK-SNAP starts with several generic labels
   ("Label 1"…); you'll rename/replace them.

2. **Create and name every label.** For each structure:
   - Reuse an existing default label with `rename_label(id, "Name")`, or
   - `create_label(name, r, g, b)` to add a new one with a distinct colour.
   Give each a **visually distinct colour** (`set_label_color`) so the overlay is
   readable — e.g. tumor red, edema yellow, necrosis blue.

3. **Segment structures one at a time, brightest/most-distinct first.**
   For each structure, in order:
   - `set_active_label(id)` so edits land on the right label.
   - Segment it with the method that fits:
     - distinct intensity band unique to this structure → `threshold_segment`
       with that band into this label;
     - compact structure sharing intensities with neighbours → load the
       **active-contour-segmentation** skill and grow it from a seed into this label.
   - `measure_volume(id)` + `focus_label(id)` to verify before moving on.
   Doing the most intensity-distinct structure first means later, looser
   segmentations have fewer free voxels to accidentally grab.

4. **Keep structures from overwriting each other.**
   The current tool set segments into whichever label you set active; it does not
   expose ITK-SNAP's interactive "paint-over" guard. So prevent and repair overlaps
   with these concrete moves:
   - **Prevent:** give each structure a **non-overlapping intensity band** where you
     can. Two tissues with the same intensity cannot be separated by threshold alone —
     use the snake (seed inside each) so growth stays regional.
   - **Repair:** if structure B's segmentation swallowed some of structure A, use
     `replace_label(from_label=B, to_label=A)` only where appropriate, or
     `clear_label(B)` and redo B with a tighter band/seed. `count_voxels` before/after
     to confirm the fix.
   - Segment into a **fresh label id per structure** — never reuse one id for two
     structures.

5. **Review the whole map together.** `set_layout("all")`, set a readable overlay
   opacity (`set_segmentation_opacity(50)`), and toggle individual labels with
   `set_label_visibility` / `set_label_opacity` to check boundaries between touching
   structures. `update_3d_mesh` + `set_layout("3d")` to inspect the 3D arrangement and
   catch stray voxels.

6. **Measure them all.** `measure_all_labels` → a combined table of every structure's
   volume in mL. If the user wants it saved, `save_statistics(path)` writes the whole
   table at once, and `save_workspace(path)` preserves the labelled result.

7. **Report** the per-structure volumes as a table and note any structures that share
   a boundary you had to repair.

## Pitfalls (from real multi-label practice)
- **One label id reused for two structures** → they merge irreversibly. Always a fresh
  id per structure.
- **Two tissues, same intensity, threshold only** → each threshold grabs both. Use the
  snake with a seed inside each, or accept that they can't be separated by intensity.
- **Not verifying between structures** → a leak in structure 2 quietly eats structure
  1. `measure_volume` + `focus_label` after each one.
- **Unreadable overlay** → set distinct colours and ~50% opacity before reviewing.
- **Forgetting the combined export** → if the user wanted "all volumes", use
  `measure_all_labels` + `save_statistics`, not one label at a time.
