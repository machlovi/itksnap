# RESEARCH — ITK-SNAP LLM Agent (Phase 1)

> **Scope note (see ASSUMPTIONS.md A1):** The task brief was written in OHIF
> Viewer / Cornerstone3D terms. This project is **ITK-SNAP**. Section 5 maps the
> OHIF concepts to ITK-SNAP's real API so the brief's intent is preserved.

ITK-SNAP is a C++/Qt desktop application (no embedded scripting console). Our
agent drives it through an embedded Qt dock → local Python "sidecar" (the agent
brain, backend-agnostic, currently the user's `localmodel` on llama.cpp) →
tool-calls executed against ITK-SNAP's C++ Logic layer (`IRISApplication`).

---

## 1. Common user workflows (what users actually do)

| Workflow | How users do it in ITK-SNAP | Agent tool(s) |
|---|---|---|
| Load a study | File → Open Main Image (NIfTI/DICOM/MHA/NRRD); DICOM series import | `load_image` |
| Load overlays / compare modalities | Add as overlay; Layer Inspector to toggle/contrast | `load_overlay`, `unload_overlays` |
| Navigate | 3 orthogonal slice views + 3D view; crosshair | `move_cursor`, `focus_label`, `set_layout` |
| Window/level (contrast) | Layer Inspector → Contrast (intensity curve) | *(not yet wired — IntensityCurveModel)* |
| Manual segmentation | Polygon tool, Paintbrush, label management | *(interactive; agent uses threshold instead)* |
| Semi-automatic segmentation | "Snake": preprocessing → bubble seeds → Region-Competition / Edge evolution | *(complex pipeline; agent uses threshold + smooth/interpolate)* |
| Threshold segmentation | Preprocessing → Thresholding | `threshold_segment` |
| Label management | Label Editor (add/rename/recolor), active drawing label | `rename_label`, `set_label_color`, `set_active_label`, `create` via existing 1–6 |
| Clean up a segmentation | Smooth labels; Interpolate between drawn slices | `smooth_labels`, `interpolate_labels` |
| Measurements | Segmentation → Volumes & Statistics; export | `measure_volume`, `measure_all_labels`, `count_voxels`, `save_statistics` |
| 3D rendering | Update mesh → 3D view; export mesh | `update_3d_mesh` |
| Save session | Save Workspace (`.itksnap`); save labels/annotations; export slice | `save_workspace`, `load_workspace`, `save_labels`, `save_annotations`, `export_slice` |
| Undo/redo edits | Ctrl-Z / Ctrl-Y | `undo`, `redo` |

**Sources**
- Official tutorial (8 sections: load, view, manual seg, snakes, volumes/stats): http://www.itksnap.org/docs/viewtutorial.php
- Full manual (region-competition/edge snakes, overlays, workspaces): http://www.itksnap.org/docs/fullmanual.php
- Getting-started hub (videos, short course, shortcuts, CLI): http://www.itksnap.org/pmwiki/pmwiki.php?n=Documentation.SNAP3
- Method paper (semi-automatic snake), Yushkevich et al.: https://pmc.ncbi.nlm.nih.gov/articles/PMC5493443/
- Community walkthroughs: https://neuroimaging-core-docs.readthedocs.io/en/latest/pages/itk-snap.html · https://andysbrainbook.readthedocs.io/en/latest/ITK-Snap/ITK-Snap_Short_Course/ITK-Snap_02_GUI.html

## 2. Tutorials, examples, datasets
- Example datasets (tutorial MRI + segmentation; structural+seg+fMRI tmap): http://www.itksnap.org/pmwiki/pmwiki.php?n=Documentation.DataSets
- Tutorial image download (MRI-crop): http://www.itksnap.org/download/snap/files/MRI-crop.zip
- 90-minute short course announcement (videos, handouts, data): https://picsl.upenn.edu/itk-snap-3-6-tutorial-available-online/
- RSNA 2017 hands-on exercises (PDF): http://www.itksnap.org/pmwiki/uploads/Train/RSNA2017-Handout-Exercises.pdf
- Training videos: https://www.youtube.com/watch?v=p9oQJ51E4_Q (manual seg) · https://www.youtube.com/watch?v=xX5HpT67ico (multi-modality)

## 3. Automation / scripting surface
ITK-SNAP has **no in-app scripting console**. Programmatic control comes from:
- **Command-line tools** — `itksnap` (launcher), **`itksnap-wt`** (workspace tool: add/list layers, tags, DSS tickets), `c3d`/`c2d`/`c4d` (Convert3D), `greedy` (registration).
  http://www.itksnap.org/pmwiki/pmwiki.php?n=Documentation.CommandLine · http://www.itksnap.org/pmwiki/pmwiki.php?n=Convert3D.Convert3D
  `itksnap-wt` examples: https://discourse.itk.org/t/how-to-manipulate-itksnap-workspace-file-itksnap-via-itksnap-command-line-tool-itksnap-wt/5699
- **Distributed Segmentation Service (DSS)** — ticket-based remote segmentation (ITK-SNAP 3.8+).
  https://alfabis-server.readthedocs.io/en/latest/user_quick_start.html · https://alfabis-server.readthedocs.io/en/latest/service_quick_start.html · https://github.com/pyushkevich/alfabis_server
- **Deep-learning plugin `itksnap-dls`** — nnInteractive point/scribble/lasso AI segmentation, local or remote GPU (ITK-SNAP 4.4+).
  https://github.com/pyushkevich/itksnap-dls · https://itksnap-dls.readthedocs.io/en/latest/quick_start.html
- **Our approach (this project):** a forked ITK-SNAP with an embedded Qt dock that calls the **C++ `IRISApplication` Logic API directly** — the richest surface, since it reaches everything the GUI can, in-process. (Repo: https://github.com/pyushkevich/itksnap)

## 4. The C++ automation API we drive (per tool)
Core entry: `GlobalUIModel::GetDriver()` → `IRISApplication*`. Key classes:
`IRISApplication` (Logic/Framework), `ColorLabelTable` + `SegmentationStatistics`
(Logic/Common), `GlobalState` (Logic/Framework), `SegmentationUpdateIterator`
(Logic/Framework), GUI sub-models via `GlobalUIModel` (`DisplayLayoutModel`,
`Generic3DModel`, `SmoothLabelsModel`, `InterpolateLabelModel`,
`IntensityCurveModel`).

## 5. OHIF / Cornerstone3D → ITK-SNAP mapping (intent → API call)

| OHIF / Cornerstone3D concept | ITK-SNAP equivalent | Exact API |
|---|---|---|
| `CommandsManager.runCommand(name, opts)` | hand-wired tool dispatch (no runtime command bus) | `AssistantPanel::dispatchToolCall(name,args)` → `toolXxx()` |
| `DisplaySetService` (loaded series/metadata) | layer container on the driver | `driver->GetCurrentImageData()->GetMain()->GetSize()`, `GetDefaultScalarRepresentation()->GetImageMin/MaxAsDouble()`, `IsMainImageLoaded()` |
| active/selected series | selection state | `GlobalState::GetSelectedLayerId()/GetSelectedSegmentationLayerId()`, `driver->GetSelectedSegmentationLayer()` |
| `MeasurementService` (ROI stats/export) | segmentation statistics | `SegmentationStatistics::Compute(driver)`+`GetStats()` (`count`,`volume_mm3`,`mean`,`stdev`); `ExportSegmentationStatistics(file)`; `GetNumberOfVoxelsWithLabel(l)` |
| `SegmentationService.addSegmentation` | load/blank a labelmap | `OpenImage(path, LABEL_ROLE, wl)`; `AddBlankSegmentation()`; `UpdateIRISSegmentationImage(...)` |
| `SegmentationService` edit/paint | region write iterator | `SegmentationUpdateIterator(seg, region, label, drawOver)` + `PaintAsForeground()` + `Finalize()` |
| `SegmentationService` relabel/clear | whole-image label ops | `ReplaceLabel(new,old)`; `ResetIRISSegmentationImage()`; `ReplaceLabel(0,l)` (clear one) |
| segment metadata (name/color/active) | label table + draw state | `ColorLabelTable::GetColorLabel/SetColorLabel`, `ColorLabel::SetLabel/SetRGB`; `GlobalState::SetDrawingColorLabel` |
| `ViewportGridService.setLayout` | view-panel layout | `DisplayLayoutModel::SetViewPanelLayout(VIEW_ALL/AXIAL/CORONAL/SAGITTAL/VIEW_3D)` |
| `HangingProtocolService` (rule-driven presets) | **GAP** — only manual layout + contrast/colormap presets exist | — |
| Cornerstone3D window/level (WW/WL) | intensity curve | `IntensityCurveModel` window/level range models; `OnAutoFitWindow()` *(not yet wired)* |
| Cornerstone3D threshold tool | intensity-range paint | `CreateCastToFloatPipeline()` + `SegmentationUpdateIterator` (`threshold_segment`) |
| Cornerstone3D brush/manual | paintbrush | `PaintbrushModel` + `GlobalState::SetPaintbrushSettings` *(interactive; not agent-wired)* |
| smooth / interpolate labels | post-processing models | `SmoothLabelsModel::Smooth(...)`; `InterpolateLabelModel::Interpolate()` |
| DICOMweb state reads | in-memory getters (no network) | `GetCursorPosition()`, `GetNumberOfVoxelsWithLabel()`, `SegmentationStatistics`, label-at-cursor via `seg->GetModifiableImage()->GetPixel(idx)` |
| undo/redo | driver undo stack | `Undo()/Redo()` guarded by `IsUndoPossible()/IsRedoPossible()` |
| study export / session save | project + slice + annotations | `SaveProject/OpenProject`, `ExportSlice`, `SaveAnnotations/LoadAnnotations` |
| 3D/VR render | mesh rebuild | `Generic3DModel::UpdateSegmentationMesh(NULL)`; `ExportSegmentationMesh(...)` |

### Gaps (OHIF concepts with no clean ITK-SNAP equivalent)
- **HangingProtocolService** — no metadata-rule-driven auto-layout; only manual layout + contrast/colormap presets.
- **CommandsManager registry** — no runtime command bus; every capability is compiled C++ reached via the hand-written dispatch.
- **DICOMweb / QIDO-RS / WADO-RS / STOW (DICOM SEG)** — ITK-SNAP reads local files/DICOM dirs and can scp/sftp; there is no DICOMweb client or segmentation push-back.
- **First-class measurement annotations** (length/angle/probe as persisted numeric records) — ITK-SNAP has ruler/landmark annotations and separately segmentation statistics, but no unified MeasurementService object model.
- **Segmentation representation interop** (DICOM SEG / RTSTRUCT, contour vs labelmap) — ITK-SNAP segmentation is a single labelmap; no contour/DICOM-SEG round-trip.

---

## 6. Verification strategy that this research implies
Because ITK-SNAP state is all local/in-memory and reachable via `IRISApplication`,
ground truth can be read **directly and cheaply** (label voxel counts, per-label
intensity ranges, cursor, undo state) — no DICOMweb round-trip needed. This is
what the headless `assistant_eval` harness (Phase 4) exploits: it runs the real
`IRISApplication` and reads authoritative state after each tool, so an LLM's
textual claim of success can be checked against the actual labelmap.
