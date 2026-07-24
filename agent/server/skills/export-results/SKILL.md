---
name: export-results
description: >
  Save and export a session's results for reproducibility or downstream analysis — the
  workspace, the statistics table, label descriptions, a slice snapshot, ruler/landmark
  annotations, and the 3D mesh view. Use when the user asks to save, export, back up,
  hand off, or "write everything to disk", or to prepare results for radiomics /
  another tool / a report.
---

# Export & Save Results

The goal is a **complete, reproducible set of artifacts** so the work reopens exactly
and feeds the next step. Figure out which artifacts the user needs and write each with
its dedicated tool — one "save" rarely covers everything.

## Decide what to export
Ask (or infer from the request) which of these are wanted; when the user says
"everything" or "back up the session", do all that apply:
- **Reproducible session** → workspace (`.itksnap`).
- **Numbers for a report / spreadsheet / radiomics** → statistics table (`.csv`).
- **Label names & colours** (to reuse the labelling scheme on another scan) → label
  descriptions (`.txt`).
- **A picture for a report/slide** → slice snapshot (`.png`).
- **Ruler/landmark measurements** → annotations file.

## Procedure

1. **Confirm the output location.** Use the folder/paths the user gave, exactly. If
   none given, propose a folder and self-describing, space-free names
   (`subj01_workspace.itksnap`, `subj01_stats.csv`, `subj01_labels.txt`,
   `subj01_axial.png`).

2. **Save the workspace** — `save_workspace(path)` → `.itksnap`. This bundles the main
   image reference, segmentation, and labels so the whole session reopens with
   `load_workspace`. This is the single most important artifact for reproducibility.

3. **Export the statistics** — `save_statistics(path)` → `.csv`. Writes per-label
   volume, voxel count, and intensity stats to disk. (Remember: `measure_all_labels`
   only prints to chat; this is the one that writes the file the user can open.)

4. **Save label descriptions** — `save_labels(path)` → `.txt`. The number→name→colour
   map, reusable via `load_labels` on another scan and compatible with Convert3D
   (`c3d`) for downstream processing.

5. **Capture a snapshot** — with the view centred (`focus_label` / `move_cursor`) and a
   readable opacity (`set_segmentation_opacity(50)`), `export_slice(direction, path)` →
   `.png` for the report/slide.

6. **Save annotations** — if the user placed rulers/landmarks, `save_annotations(path)`.

7. **3D view** — if a 3D figure is wanted, `update_3d_mesh` then `set_layout("3d")` so
   the mesh is current; capture via `export_slice` if a still is needed. (Note: there
   is no surface-mesh file export tool in the current set — for an actual `.stl`/`.vtk`
   mesh file, the user exports from ITK-SNAP's `Segmentation → Export as Surface Mesh`
   or via `vtklevelset`/`c3d`. Say so rather than implying a mesh file was written.)

8. **Confirm every path written.** List back each file you actually saved and what it
   contains, so the user can verify the handoff.

## Reload / round-trip
To restore a session: `load_workspace(path)` (image+seg+labels), or piecewise
`load_image` + `load_segmentation` + `load_labels` + `load_annotations`. Mention this
so the user knows the exports are round-trippable.

## Pitfalls
- **One "save" ≠ everything.** Workspace, stats, labels, snapshot, annotations are
  separate files with separate tools — write each the user needs.
- **measure vs save.** Chat numbers are not a file; call `save_statistics` for the CSV.
- **Overwriting.** If a path already exists and the user didn't say to overwrite,
  choose a new name or confirm first.
- **Implying a mesh file export.** The current tools don't write `.stl`/`.vtk`; be
  honest and point to the GUI/CLI path for that.
- **Compound requests.** If several exports were asked for in one message, re-read and
  confirm each was written before finishing.
