"""The 33 ITK-SNAP tool schemas the dock registers (mirror of
AssistantPanel::toolSchemas). Used by the eval driver so the LLM sees exactly
what the real dock advertises."""
def _t(n, d, p=None, r=None):
    s = {"type": "object", "properties": p or {}}
    if r: s["required"] = r
    return {"name": n, "description": d, "input_schema": s}
N = lambda d: {"type": "number", "description": d}
I = lambda d: {"type": "integer", "description": d}
Sd = lambda d: {"type": "string", "description": d}

TOOLS = [
  _t("get_scene_overview", "Report the current ITK-SNAP state: whether an image is loaded, its dimensions, intensity range, cursor position, and the defined segmentation labels. Call this first when unsure of state or to pick an intensity range."),
  _t("get_cursor_info", "Report the current crosshair voxel position and which segmentation label is under it."),
  _t("measure_volume", "Measure the volume in mL (and voxel count) of ONE segmentation label.", {"label": I("Label id (default 1).")}),
  _t("measure_all_labels", "Measure the volume in mL of EVERY non-empty segmentation label at once."),
  _t("count_voxels", "Count how many voxels currently carry a given label.", {"label": I("Label id.")}, ["label"]),
  _t("load_image", "Load a main medical image volume into ITK-SNAP from an absolute file path.", {"path": Sd("Absolute path to the image file.")}, ["path"]),
  _t("load_overlay", "Load an additional image as an overlay on top of the main image.", {"path": Sd("Path to overlay.")}, ["path"]),
  _t("load_segmentation", "Load an existing segmentation (label image) from a file.", {"path": Sd("Path to label image.")}, ["path"]),
  _t("unload_overlays", "Remove all overlay images, leaving only the main image."),
  _t("threshold_segment", "Segment all voxels whose intensity is in [lower, upper] into a label and paint it into the viewer. Use get_scene_overview first to choose a range.",
     {"lower": N("Minimum intensity."), "upper": N("Maximum intensity (optional; defaults to image max)."), "label": I("Label id (1-6 exist by default; default 1)."), "name": Sd("Human name for the structure.")}, ["lower"]),
  _t("clear_segmentation", "Erase the ENTIRE segmentation (all labels) and start blank."),
  _t("clear_label", "Erase only the voxels belonging to one label, leaving other labels intact.", {"label": I("Label id to erase.")}, ["label"]),
  _t("replace_label", "Replace every voxel of one label with another label id.", {"from_label": I("Existing label."), "to_label": I("Label to replace it with (0 = clear).")}, ["from_label", "to_label"]),
  _t("set_active_label", "Set the active drawing label.", {"label": I("Label id.")}, ["label"]),
  _t("rename_label", "Rename a segmentation label.", {"label": I("Label id."), "name": Sd("New name.")}, ["label", "name"]),
  _t("set_label_color", "Set the RGB color of a segmentation label.", {"label": I("Label id."), "r": I("0-255"), "g": I("0-255"), "b": I("0-255")}, ["label", "r", "g", "b"]),
  _t("move_cursor", "Move the ITK-SNAP crosshair to a voxel coordinate.", {"x": I("X voxel."), "y": I("Y voxel."), "z": I("Z voxel.")}, ["x", "y", "z"]),
  _t("focus_label", "Center the ITK-SNAP view on the middle of a segmentation label.", {"label": I("Label id to center on.")}, ["label"]),
  _t("set_layout", "Change the ITK-SNAP view layout.", {"layout": {"type": "string", "enum": ["all", "axial", "coronal", "sagittal", "3d"]}}, ["layout"]),
  _t("update_3d_mesh", "Rebuild the 3D surface mesh from the current segmentation."),
  _t("smooth_labels", "Smooth the boundary of a segmentation label with a Gaussian.", {"label": I("Label id (default 1)."), "sigma_mm": N("Sigma in mm (default 1).")}),
  _t("interpolate_labels", "Interpolate a segmentation across slices that were only labeled on some slices.", {"label": I("Label id (optional; omit to interpolate all).")}),
  _t("export_slice", "Save the current slice (at the crosshair) as a 2D image file.", {"direction": {"type": "string", "enum": ["axial", "coronal", "sagittal"]}, "path": Sd("Output path.")}, ["direction", "path"]),
  _t("save_workspace", "Save the current ITK-SNAP workspace to a .itksnap file.", {"path": Sd("Path for the .itksnap file.")}, ["path"]),
  _t("load_workspace", "Open an ITK-SNAP workspace (.itksnap) file.", {"path": Sd("Path to .itksnap file.")}, ["path"]),
  _t("save_statistics", "Write/export per-label statistics to a FILE ON DISK at the given path.", {"path": Sd("Path for the statistics file.")}, ["path"]),
  _t("unload_main_image", "Close the current main image."),
  _t("save_annotations", "Save ruler/landmark annotations to a file.", {"path": Sd("Path.")}, ["path"]),
  _t("load_annotations", "Load ruler/landmark annotations from a file.", {"path": Sd("Path.")}, ["path"]),
  _t("save_labels", "Save the label descriptions (names/colors) to a text file.", {"path": Sd("Path.")}, ["path"]),
  _t("load_labels", "Load label descriptions from a text file.", {"path": Sd("Path.")}, ["path"]),
  _t("undo", "Undo the last segmentation edit."),
  _t("redo", "Redo the last undone segmentation edit."),
]
assert len(TOOLS) == 33, len(TOOLS)
