# ITK-SNAP Capability Audit → Tool Design

Source-accurate audit of what each ITK-SNAP surface can actually do, and the
tool set derived from it. "Scriptable" = callable from the dock via a logic/model
method (no mouse); "Interactive" = needs on-screen pixel events.

## A. Capability inventory (by surface)

### Main 2D toolbar — mode select is scriptable, strokes are not
`GlobalState::SetToolbarMode(ToolbarModeType)` switches: CROSSHAIRS, NAVIGATION
(zoom/pan), POLYGON, PAINTBRUSH, ROI (snake), ANNOTATION. 3D modes via
`SetToolbarMode3D`: TRACKBALL, CROSSHAIRS_3D, SPRAYPAINT, SCALPEL.

### Cursor inspector — read-rich (`CursorInspectionModel`)
Voxel coord (read/write via `GetCursorPositionModel`), time point, **label id +
name under cursor**, **per-layer intensity value(s) + displayed color**. (This
build shows voxel coords only, no world-mm field — mapping exists via
`GenericSliceModel::MapSliceToImagePhysical`.)

### View / zoom — fully scriptable (`SliceWindowCoordinator`)
`ResetViewToFitInAllWindows`, `CenterViewOnCursorInAllWindows`,
`SetZoomPercentageInAllWindows(x)`, `LinkedZoom` toggle; per-window zoom/pan/slice.

### Segmentation labels — fully scriptable
- Per label (`ColorLabel` via `ColorLabelTable::Get/SetColorLabel`): `SetAlpha`
  (opacity), `SetVisible` (2D), `SetVisibleIn3D`, `SetLabel` (name), `SetRGB`.
- Table (`ColorLabelTable`): `SetColorLabelValid` (create/delete), `GetInsertionSpot`,
  `InitializeToDefaults`, `RemoveAllLabels`.
- Editor (`LabelEditorModel`): `MakeNewLabel`, `DeleteCurrentLabel`,
  `ReassignLabelId`, `SetAllLabelsVisibility[In3D]`.
- Draw context (`GlobalState`): `SetDrawingColorLabel`, `SetDrawOverFilter`,
  `SetCoverageMode`, `SetSegmentationAlpha` (global overlay opacity).
- Whole-image: `ReplaceLabel(A,B)`, `GetNumberOfVoxelsWithLabel`.

### Paintbrush — settings scriptable, stroke interactive (`PaintbrushSettingsModel`)
shape, size, smart mode (manual/watershed/DLS), volumetric/isotropic, watershed level.

### Polygon — FULLY automatable (`PolygonDrawingModel`)
`SetVertices(list)` + `AcceptPolygon()` commits a polygon from coordinates — the one
manual-seg primitive an agent can drive end-to-end. Plus `ClosePolygon`, `Reset`.

### Semi-automatic "snake" / active contour — FULLY scriptable end-to-end ★
`IRISApplication`: `InitializeSNAPImageData(roi)` → `SetSnakeMode(IN_OUT_SNAKE|EDGE_SNAKE)`
→ `EnterPreprocessingMode(mode)` + `ApplyCurrentPreprocessingModeToSpeedVolume()` (sets
`SpeedValid`) → seeds via `GlobalState::SetBubbleArray({center voxel, radius mm})` →
`InitializeActiveContourPipeline()` → loop `SnakeWizardModel::PerformEvolutionStep()`
(until converged/N) → `UpdateIRISWithSnapImageData()` → `ReleaseSNAPImageData()`.
Threshold params on `SnakeWizardModel`: `ThresholdLower/UpperModel`, `ApplyPreprocessing()`.

### 3D — mesh + cut-plane scriptable (`Generic3DModel` / `IRISApplication`)
`UpdateSegmentationMesh`, `ContinuousUpdate`, `ClearRenderingAction`, `ResetView`,
`ExportMesh`. **`RelabelSegmentationWithCutPlane(normal, intercept)`** relabels across
a plane without the interactive scalpel.

### Smooth / Interpolate — scriptable
`SmoothLabelsModel::Smooth(labels, sigma, mm|vox, allFrames)`;
`InterpolateLabelModel::Interpolate()` with method (DistanceMap/LevelSet/Morphology/BWA).

### Registration — auto scriptable (`RegistrationModel`)
`RunAutoRegistration()` with `Transformation`(RIGID/AFFINE), `SimilarityMetric`(NMI/NCC/SSD),
resolution schedule, `MovingLayer`; init `MatchByMoments`/`MatchImageCenters`;
`ResliceMovingImage()`; `Load/SaveTransform`.

### Window / level — scriptable (`IntensityCurveModel`)
`OnAutoFitWindow()`; `GetIntensityRangeModel(WINDOW|LEVEL|MINIMUM|MAXIMUM)->SetValue()`;
`ApplyToLayers(ALL|ONE)`.

## B. Tool roadmap (what the agent gets)

Legend: ✅ done (pre-audit)  🆕 adding now  📋 planned

| Tool | Capability | Status |
|---|---|---|
| threshold_segment / clear_* / replace_label / measure_* / count_voxels | primitive seg + stats | ✅ |
| load_* / save_* / undo / redo / move_cursor / focus_label / set_layout / update_3d_mesh / smooth_labels / interpolate_labels | I/O, nav, edit | ✅ |
| **active_contour_segment** | ★ snake pipeline (region/edge, threshold preproc, bubble seeds, evolve, commit) | 🆕 |
| **auto_window_level** / **set_window_level** | contrast | 🆕 |
| **set_label_opacity** / **set_label_visibility** / **set_segmentation_opacity** | display control | 🆕 |
| **inspect_cursor** | rich cursor readout (label + per-layer intensity) | 🆕 |
| **create_label** / **delete_label** | label lifecycle | 🆕 |
| **register_images** | auto rigid/affine registration | 🆕 |
| **relabel_cut_plane** | 3D plane relabel | 🆕 |
| set_draw_over / set_coverage / set_zoom / set_active_tool / draw_polygon+accept / export_mesh / configure_paintbrush | draw context, view, polygon, mesh | 📋 |

## C. Skills that now earn `use_skill` (non-obvious procedures)
- **snake-segment** — the multi-step active-contour protocol (the model cannot guess the
  ROI→preproc→seed→evolve→commit ordering; loading the skill is genuinely useful).
- **register-and-compare** — load overlay → register → reslice → compare.
- **prep-display** — auto window/level → set layout → opacity.
