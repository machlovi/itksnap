---
name: lesion-volumetry-report
description: >
  Produce a complete volumetric report for a lesion/tumor/organ: segment it, measure
  its volume (mL) and voxel count, capture a snapshot, and write the statistics and a
  reproducible workspace to disk. Use whenever the user wants to "measure and report",
  "quantify", "get the volume and save it", "export stats", "workup with numbers", or
  asks for a volume they can put in a report / spreadsheet / radiomics pipeline. This
  is the segment→measure→export pipeline, not just an on-screen number.
---

# Lesion / Tumor Volumetry Report

The deliverable is a **saved, reproducible measurement**, not just a number in chat.
Real volumetry work (tumor tracking, radiomics handoff, clinical reporting) always
ends with files on disk. Skipping the save/export step is the most common way this
task is done wrong — so treat the export steps as mandatory when the user says
"report", "save", or "export".

## Inputs to confirm first
- Is an image loaded? (`get_scene_overview`.) If not, `load_image` from the path the
  user gave, or ask for one and stop.
- Is the structure already segmented, or do we need to segment it now?
- Did the user give an output path/folder? If yes, honour it exactly. If not, keep the
  numbers in chat and offer to save, or use a sensible default the user can change.

## Procedure

1. **Read the scene.** `get_scene_overview` — confirm image loaded; note intensity
   min/max and existing labels. If the target is already a label, skip to step 4.

2. **Segment the target** (if not already done). Choose the method:
   - Compact structure among similar-intensity neighbours, or the user said
     active-contour/snake → use the **active-contour-segmentation** skill
     (`use_skill`), which grows a clean connected region.
   - Target is the only thing in its intensity band → `threshold_segment` with a
     band from the overview, into a named label.
   Give the label a real name up front: `rename_label(label, "Lesion")` (or Tumor,
   Kidney, etc.) so every downstream number and file is self-describing.

3. **Verify the segmentation before you measure it.** `focus_label` to centre the
   view. A volume you didn't look at is a volume you can't trust. If it obviously
   leaked or under-filled, fix it (undo + re-segment, or `smooth_labels`) BEFORE
   reporting — a wrong number in a saved report is worse than no report.

4. **Measure.**
   - Single structure: `measure_volume(label)` → mL + voxel count.
   - Several structures / whole segmentation: `measure_all_labels` → every non-empty
     label's volume at once.
   - Need a raw count for a specific label: `count_voxels(label)`.

5. **Capture a visual record (optional but recommended for a "report").**
   With the view centred on the lesion (`focus_label`), `export_slice` the most
   informative orientation (usually `axial`) to a PNG next to the stats file, so the
   report has a picture, not just numbers.

6. **Export the statistics to disk.** `save_statistics(path)` — this WRITES the
   per-label table (volume, voxel count, intensity stats) to a file. `measure_*`
   only reports in chat; `save_statistics` is what fulfils "save/export/write the
   measurements". Use a `.csv` path (e.g. `<folder>/lesion_stats.csv`) so it opens in
   a spreadsheet / feeds radiomics.

7. **Save a reproducible workspace.** `save_workspace(path)` to a `.itksnap` file so
   the image + segmentation + labels reopen exactly as measured. For a lesion this is
   the artifact that makes the number auditable later.

8. **Report back.** State: structure name, **volume in mL**, voxel count, the method
   used (threshold band or snake), and the exact file paths written (stats CSV,
   workspace, snapshot). If you measured several labels, give the table.

## Compound requests — finish every part
If the user asked for several things in one message ("measure the tumor AND the edema
AND save the stats to D:/out"), complete each with its own tool call and re-read the
request before finishing to confirm nothing — especially a save/export path — was
skipped.

## Pitfalls
- **Reporting an unverified volume.** Always `focus_label` and sanity-check the mL
  against the plausible size before saving.
- **Confusing measure with save.** `measure_all_labels` does NOT write a file;
  `save_statistics` does. If the user said "save/export", you must call it.
- **Volume drift after cleanup.** If you `smooth_labels` or edit after measuring,
  re-`measure_volume` so the saved number matches the final mask.
- **Units.** ITK-SNAP reports volume in mm³/mL; 1 mL = 1 cm³ = 1000 mm³. Report mL.

For what a good volumetric report file should contain and CSV column conventions,
load `reference.md` with `read_skill_file`.
