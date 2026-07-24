"""Generate test_scenarios.json for the ITK-SNAP agent eval.

Phantom facts (make_phantom.py): 128^3, background=40, organ=100 (~378k vox),
nodule=220 (~2969 vox), image range 40..220.

Scenario schema:
  id, prompt, difficulty(1-10), category,
  setup:            [{tool,args}]   applied DIRECTLY (ground truth) before the prompt
  expected_actions: [tool names the LLM must call]  (subset match, order-tolerant)
  expected_state:   assertion dict checked against real IRISApplication state after
  verifiable:       bool  (is expected_state ground-truth-checkable headlessly?)

expected_state assertion keys:
  loaded / not_loaded : bool
  labels_present / labels_absent : [ids]
  label : { "<id>": {voxels_gt, voxels_between:[lo,hi], imin_gte, imax_lte, name} }
  cursor_eq : [x,y,z]        cursor_changed : bool
  undo_available : bool      n_valid_labels_gte : int
  file_written : "<path>"    (checked with os.path.exists by the driver)
"""
import json, re

def _path(p):
    """Robustly extract a D:/... style path from a prompt (avoids grabbing
    trailing words like 'a workspace')."""
    m = re.search(r'[A-Za-z]:/[^\s]+', p)
    return m.group(0) if m else p

SC = []
_id = [0]
def S(prompt, actions, difficulty, category, expected_state=None, setup=None, verifiable=None):
    _id[0] += 1
    if verifiable is None:
        verifiable = expected_state is not None
    SC.append({
        "id": f"S{_id[0]:03d}",
        "prompt": prompt,
        "difficulty": difficulty,
        "category": category,
        "setup": setup or [],
        "expected_actions": actions,
        "expected_state": expected_state or {},
        "verifiable": verifiable,
    })

# convenient setups
SEG_NODULE = [{"tool": "threshold_segment", "args": {"lower": 200, "upper": 255, "label": 1, "name": "nodule"}}]
SEG_ORGAN  = [{"tool": "threshold_segment", "args": {"lower": 90, "upper": 110, "label": 2, "name": "organ"}}]
SEG_BOTH   = SEG_NODULE + SEG_ORGAN

# =====================================================================
# A. PER-TOOL — 5 real-world prompts each (varied phrasing + edge cases)
# =====================================================================

# ---- load_image ---- (unload first so loading is a real transition; give a path)
_PH = "D:/itksnap-build/phantom.nii.gz"
_UNLOAD = [{"tool": "unload_main_image", "args": {}}]
for p in [f"load the image at {_PH}", f"open the scan {_PH}",
          f"I want to work on the volume, load {_PH}",
          f"bring up the main image from {_PH}", f"open the file {_PH}"]:
    S(p, ["load_image"], 1, "tool:load_image", {"loaded": True}, setup=_UNLOAD)

# ---- get_scene_overview ----
for p in ["what's currently loaded?", "give me an overview of the scene",
          "describe the current state of the viewer", "what am I looking at right now?",
          "summarize what's in itk-snap at the moment"]:
    S(p, ["get_scene_overview"], 1, "tool:get_scene_overview", {"loaded": True})

# ---- threshold_segment (varied ranges → deterministic phantom outcomes) ----
S("segment everything brighter than 200 as label 1", ["threshold_segment"], 2, "tool:threshold_segment",
  {"labels_present": [1], "label": {"1": {"voxels_gt": 0, "imin_gte": 200, "imax_lte": 255}}})
S("outline the bright nodule for me", ["threshold_segment"], 2, "tool:threshold_segment",
  {"labels_present": [1], "label": {"1": {"imin_gte": 180}}})
S("segment the organ tissue around intensity 100 into label 2", ["threshold_segment"], 3, "tool:threshold_segment",
  {"labels_present": [2], "label": {"2": {"voxels_gt": 1000, "imin_gte": 80, "imax_lte": 120}}})
S("threshold between 40 and 60 and call it background", ["threshold_segment"], 3, "tool:threshold_segment",
  {"label": {"1": {"imin_gte": 40, "imax_lte": 60}}})
S("mark all voxels with value 220 exactly as a lesion in label 3", ["threshold_segment"], 3, "tool:threshold_segment",
  {"labels_present": [3], "label": {"3": {"imin_gte": 219, "imax_lte": 221}}})

# ---- measure_volume (needs a seg → setup) ----
for p, st in [("how big is label 1?", SEG_NODULE), ("what's the volume of the nodule in mL?", SEG_NODULE),
              ("measure label 2", SEG_ORGAN), ("give me the size of the segmented organ", SEG_ORGAN),
              ("report the volume of structure 1", SEG_NODULE)]:
    lab = 2 if st is SEG_ORGAN else 1
    S(p, ["measure_volume"], 2, "tool:measure_volume", {"labels_present": [lab]}, setup=st)

# ---- measure_all_labels ----
for p in ["measure every label", "give me all the volumes", "what are the sizes of all structures?",
          "report volumes for each segmentation", "tabulate all label volumes"]:
    S(p, ["measure_all_labels"], 2, "tool:measure_all_labels", {"labels_present": [1, 2]}, setup=SEG_BOTH)

# ---- count_voxels ----
for p in ["how many voxels are in label 1?", "count the voxels of the nodule",
          "voxel count for structure 1", "how many pixels did label 1 capture?",
          "tell me the raw voxel count of label 1"]:
    S(p, ["count_voxels"], 2, "tool:count_voxels", {"labels_present": [1]}, setup=SEG_NODULE)

# ---- clear_segmentation ----
for p in ["erase the whole segmentation", "start the segmentation over from scratch",
          "wipe all labels", "clear everything I've segmented", "reset the segmentation completely"]:
    S(p, ["clear_segmentation"], 2, "tool:clear_segmentation", {"labels_absent": [1, 2]}, setup=SEG_BOTH)

# ---- clear_label ----
for p in ["erase label 1 only", "remove the nodule segmentation but keep the organ",
          "delete just label 1's voxels", "get rid of structure 1", "clear label 1"]:
    S(p, ["clear_label"], 3, "tool:clear_label", {"labels_absent": [1], "labels_present": [2]}, setup=SEG_BOTH)

# ---- replace_label ----
for p in ["move everything in label 1 into label 2", "merge label 1 into label 2",
          "relabel structure 1 as label 2", "change label 1 voxels to label 2",
          "reassign label 1 to be label 2"]:
    S(p, ["replace_label"], 4, "tool:replace_label", {"labels_absent": [1], "labels_present": [2]}, setup=SEG_BOTH)

# ---- set_active_label ----
for p in ["make label 3 the active drawing label", "switch the current label to 3",
          "set the drawing color to label 3", "I want to paint with label 3 next",
          "activate label 3 for drawing"]:
    S(p, ["set_active_label"], 2, "tool:set_active_label", {}, verifiable=False)

# ---- rename_label ----
for p in ["rename label 1 to liver", "call label 1 'tumor'", "change the name of label 1 to kidney",
          "label 1 should be named spleen", "give label 1 the name 'lesion'"]:
    S(p, ["rename_label"], 2, "tool:rename_label", {}, setup=SEG_NODULE, verifiable=False)

# ---- set_label_color ----
for p in ["make label 1 red", "color label 1 with RGB 0 255 0", "turn label 1 blue",
          "set the color of label 1 to bright green", "recolor label 1 to 255 255 0"]:
    S(p, ["set_label_color"], 2, "tool:set_label_color", {}, setup=SEG_NODULE, verifiable=False)

# ---- move_cursor ----
S("move the crosshair to voxel 10, 20, 30", ["move_cursor"], 2, "tool:move_cursor", {"cursor_eq": [10, 20, 30]})
S("jump the cursor to 64 64 64", ["move_cursor"], 2, "tool:move_cursor", {"cursor_eq": [64, 64, 64]})
S("put the crosshair at the corner, voxel 0 0 0", ["move_cursor"], 2, "tool:move_cursor", {"cursor_eq": [0, 0, 0]})
S("navigate to position 100, 100, 90", ["move_cursor"], 2, "tool:move_cursor", {"cursor_eq": [100, 100, 90]})
S("set the cursor to the center-ish voxel 60 70 64", ["move_cursor"], 2, "tool:move_cursor", {"cursor_eq": [60, 70, 64]})

# ---- focus_label ----
for p in ["take me to the nodule", "center the view on label 1", "show me where structure 1 is",
          "focus on label 1", "jump to the middle of label 1"]:
    S(p, ["focus_label"], 3, "tool:focus_label", {"cursor_changed": True}, setup=SEG_NODULE)

# ---- undo / redo ----
for p in ["undo that", "undo my last edit", "take back the last change",
          "revert the last segmentation edit", "ctrl-z please"]:
    S(p, ["undo"], 3, "tool:undo", {"labels_absent": [1]}, setup=SEG_NODULE)
for p in ["redo it", "redo the last undone edit", "bring that back", "re-apply what I undid",
          "ctrl-y"]:
    S(p, ["redo"], 4, "tool:redo", {"labels_present": [1]},
      setup=SEG_NODULE + [{"tool": "undo", "args": {}}])

# ---- save_workspace / save_statistics / save_labels / save_annotations (file_written) ----
S("save the workspace to D:/itksnap-build/out_ws.itksnap", ["save_workspace"], 2, "tool:save_workspace",
  {"file_written": "D:/itksnap-build/out_ws.itksnap"}, setup=SEG_NODULE)
for p in ["save my session as a workspace file at D:/itksnap-build/sess.itksnap",
          "export the current project to D:/itksnap-build/proj.itksnap",
          "preserve everything to a workspace D:/itksnap-build/keep.itksnap",
          "write the itksnap workspace to D:/itksnap-build/w2.itksnap"]:
    path = _path(p)
    S(p, ["save_workspace"], 2, "tool:save_workspace", {"file_written": path}, setup=SEG_NODULE)

for i, p in enumerate(["save the statistics to D:/itksnap-build/stats1.txt",
          "export per-label measurements to D:/itksnap-build/stats2.txt",
          "write out the volume stats to D:/itksnap-build/stats3.txt",
          "dump the segmentation statistics into D:/itksnap-build/stats4.txt",
          "save a stats report at D:/itksnap-build/stats5.txt"]):
    path = _path(p)
    S(p, ["save_statistics"], 2, "tool:save_statistics", {"file_written": path}, setup=SEG_BOTH)

for p in ["save the label definitions to D:/itksnap-build/labels1.txt",
          "export label names and colors to D:/itksnap-build/labels2.txt",
          "write the label descriptions to D:/itksnap-build/labels3.txt",
          "back up the labels to D:/itksnap-build/labels4.txt",
          "store the label table at D:/itksnap-build/labels5.txt"]:
    path = _path(p)
    S(p, ["save_labels"], 2, "tool:save_labels", {"file_written": path}, setup=SEG_NODULE)

for p in ["save annotations to D:/itksnap-build/ann1.annot",
          "export the rulers/landmarks to D:/itksnap-build/ann2.annot",
          "write annotations out to D:/itksnap-build/ann3.annot",
          "persist annotations at D:/itksnap-build/ann4.annot",
          "store the annotation set in D:/itksnap-build/ann5.annot"]:
    path = _path(p)
    S(p, ["save_annotations"], 2, "tool:save_annotations", {"file_written": path})

# ---- unload_main_image ----
for p in ["close the current image", "unload the main image", "clear the loaded scan",
          "close the study", "remove the image from the viewer"]:
    S(p, ["unload_main_image"], 2, "tool:unload_main_image", {"not_loaded": True})

# ---- get_cursor_info ----
for p in ["what's at the crosshair?", "which label is under the cursor?",
          "tell me about the current cursor position", "what voxel am I on?",
          "read out the value/label at the crosshair"]:
    S(p, ["get_cursor_info"], 1, "tool:get_cursor_info", {}, verifiable=False)

# ---- SELECTION-ONLY tools (not headless-verifiable) ----
def sel(tool, prompts, difficulty=3):
    for p in prompts:
        S(p, [tool], difficulty, f"tool:{tool}", {}, verifiable=False)

sel("load_overlay", ["load an overlay from D:/x.nii.gz", "add D:/o.nii.gz as an overlay",
    "overlay the second scan D:/t2.nii.gz", "put D:/pet.nii.gz on top as overlay",
    "add an overlay image at D:/ov.nii.gz"])
sel("load_segmentation", ["load an existing segmentation from D:/seg.nii.gz",
    "import the mask D:/labels.nii.gz", "open a saved segmentation D:/s.nii.gz",
    "bring in the label image D:/m.nii.gz", "load segmentation file D:/seg2.nii.gz"])
sel("unload_overlays", ["remove all overlays", "clear the overlay images", "take off the overlays",
    "get rid of every overlay", "unload all overlay layers"])
sel("load_workspace", ["open the workspace D:/w.itksnap", "restore session from D:/s.itksnap",
    "load project D:/p.itksnap", "reopen my saved workspace D:/keep.itksnap",
    "open itksnap workspace D:/proj.itksnap"])
sel("load_labels", ["load label descriptions from D:/labels.txt", "import label names from D:/l.txt",
    "read the label table from D:/lt.txt", "restore labels from D:/labs.txt",
    "load the label definitions file D:/ld.txt"])
sel("load_annotations", ["load annotations from D:/a.annot", "import rulers from D:/r.annot",
    "open the annotation file D:/an.annot", "restore landmarks from D:/lm.annot",
    "read annotations out of D:/notes.annot"])
sel("smooth_labels", ["smooth label 1", "clean up the jagged edges of label 1 with 2mm sigma",
    "gaussian-smooth the nodule segmentation", "reduce the staircase artifacts on label 1",
    "smooth structure 1 boundaries"])
sel("interpolate_labels", ["interpolate the labels across slices", "fill the gaps between the slices I drew",
    "interpolate label 1 between slices", "connect my sparse slice drawings",
    "run slice interpolation on the segmentation"])
sel("export_slice", ["export the current axial slice to D:/slice.png", "save the coronal view to D:/c.png",
    "write the sagittal slice to D:/s.png", "screenshot the axial slice at D:/ax.png",
    "export the slice image to D:/out.png"])
sel("set_layout", ["switch to axial-only view", "show me all four panels", "go to 3D-only layout",
    "coronal view please", "single sagittal pane"])
sel("update_3d_mesh", ["rebuild the 3d mesh", "update the 3D surface", "regenerate the 3d model",
    "refresh the 3D rendering of the segmentation", "make the 3D mesh from the labels"])

# =====================================================================
# B. COMPOSITE / MULTI-TOOL (difficulty 1-10; 7-10 = >=3 sequential calls)
# =====================================================================
def C(prompt, actions, difficulty, expected_state=None, setup=None, verifiable=None):
    S(prompt, actions, difficulty, "composite", expected_state, setup, verifiable)

C("segment the bright nodule as label 1 and then measure it",
  ["threshold_segment", "measure_volume"], 5,
  {"labels_present": [1], "label": {"1": {"imin_gte": 180}}})
C("segment everything above 200 as label 1, then tell me how many voxels it has",
  ["threshold_segment", "count_voxels"], 5,
  {"labels_present": [1], "label": {"1": {"imin_gte": 200}}})
C("outline the nodule and center the view on it",
  ["threshold_segment", "focus_label"], 6,
  {"labels_present": [1], "cursor_changed": True})
C("segment the organ (around 100) as label 2 and the nodule (above 200) as label 1, then compare their volumes",
  ["threshold_segment", "measure_all_labels"], 7,
  {"labels_present": [1, 2], "label": {"1": {"imin_gte": 180}, "2": {"imin_gte": 80, "imax_lte": 120}}})
C("clear the whole segmentation, then re-segment the nodule above 210 into label 1 and measure it",
  ["clear_segmentation", "threshold_segment", "measure_volume"], 8,
  {"labels_present": [1], "label": {"1": {"imin_gte": 210}}}, setup=SEG_BOTH)
C("segment the nodule as label 1, save the workspace to D:/itksnap-build/comp1.itksnap, and save the stats to D:/itksnap-build/comp1_stats.txt",
  ["threshold_segment", "save_workspace", "save_statistics"], 9,
  {"labels_present": [1], "file_written": "D:/itksnap-build/comp1.itksnap"})
C("segment the nodule into label 1, rename it to lesion, color it red, then report its volume",
  ["threshold_segment", "rename_label", "set_label_color", "measure_volume"], 8,
  {"labels_present": [1]})
C("segment above 200 as label 1, then undo it so nothing is segmented",
  ["threshold_segment", "undo"], 7, {"labels_absent": [1]})
C("do a full workup on the bright lesion: segment it, measure it, and take me to it",
  ["threshold_segment", "measure_volume", "focus_label"], 8,
  {"labels_present": [1], "cursor_changed": True})
C("segment the organ as label 2, then erase just that label leaving everything else",
  ["threshold_segment", "clear_label"], 7, {"labels_absent": [2]})
C("segment the nodule as label 1 and the organ as label 2, then merge label 1 into label 2 and measure the result",
  ["threshold_segment", "threshold_segment", "replace_label", "measure_volume"], 9,
  {"labels_absent": [1], "labels_present": [2]})
C("segment everything above 200, move the cursor to voxel 5 5 5, then focus back on the segmentation",
  ["threshold_segment", "move_cursor", "focus_label"], 8,
  {"labels_present": [1], "cursor_changed": True})
C("give me a complete report: segment organ and nodule, measure all labels, and export the statistics to D:/itksnap-build/report_all.txt",
  ["threshold_segment", "threshold_segment", "measure_all_labels", "save_statistics"], 10,
  {"labels_present": [1, 2], "file_written": "D:/itksnap-build/report_all.txt"})
C("segment the nodule, save the labels to D:/itksnap-build/comp_labels.txt and the workspace to D:/itksnap-build/comp_ws.itksnap",
  ["threshold_segment", "save_labels", "save_workspace"], 9,
  {"file_written": "D:/itksnap-build/comp_ws.itksnap"})
C("segment the nodule as label 1, then segment the organ as label 2, then clear the whole thing and confirm it's empty",
  ["threshold_segment", "threshold_segment", "clear_segmentation"], 8, {"labels_absent": [1, 2]})
# harder generic-to-specific chains
C("prepare a clean lesion analysis end to end and persist it to D:/itksnap-build/lesion_final.itksnap",
  ["threshold_segment", "measure_volume", "save_workspace"], 9,
  {"labels_present": [1], "file_written": "D:/itksnap-build/lesion_final.itksnap"})
C("I need the nodule isolated, quantified in mL, centered in view, and its stats written to D:/itksnap-build/nodule_stats.txt",
  ["threshold_segment", "measure_volume", "focus_label", "save_statistics"], 10,
  {"labels_present": [1], "cursor_changed": True, "file_written": "D:/itksnap-build/nodule_stats.txt"})
C("segment both structures, rename label 1 to lesion and label 2 to organ, then export all volumes to D:/itksnap-build/named_stats.txt",
  ["threshold_segment", "threshold_segment", "rename_label", "rename_label", "save_statistics"], 10,
  {"labels_present": [1, 2], "file_written": "D:/itksnap-build/named_stats.txt"})
C("reset everything, segment only the exact nodule voxels (value 220) into label 1, verify by measuring it",
  ["clear_segmentation", "threshold_segment", "measure_volume"], 9,
  {"labels_present": [1], "label": {"1": {"imin_gte": 219, "imax_lte": 221}}}, setup=SEG_ORGAN)

# adversarial / negative (should NOT invent a tool)
S("rotate the 3d model 90 degrees", [], 4, "adversarial", {}, verifiable=False)
S("apply a deep learning kidney segmentation model", [], 5, "adversarial", {}, verifiable=False)
S("register this scan to an atlas", [], 5, "adversarial", {}, verifiable=False)

data = {"phantom": {"background": 40, "organ": 100, "nodule": 220, "dims": [128, 128, 128]},
        "count": len(SC), "scenarios": SC}
with open("test_scenarios.json", "w") as f:
    json.dump(data, f, indent=1)
# quick difficulty histogram
from collections import Counter
h = Counter(s["difficulty"] for s in SC)
print(f"wrote {len(SC)} scenarios; difficulty histogram:", dict(sorted(h.items())))
print("categories:", len(set(s['category'] for s in SC)))
