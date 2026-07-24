---
name: radiation-therapy-oar-contouring
description: Segment organs at risk (OAR) and target volumes (GTV/CTV/PTV) for radiation therapy treatment planning and export 3D VTK meshes.
---

# Radiation Therapy & Organ-at-Risk (OAR) Contouring

This skill provides a clinical workflow for contouring Organs at Risk (OARs) and Target Volumes (Gross Tumor Volume / Clinical Target Volume / Planning Target Volume) on CT or MRI series for radiation oncology treatment planning.

---

## Step-by-Step Procedure

### 1. Load Planning CT/MRI Image
- Call `load_image(path="...")` to load the primary radiation planning CT or MRI scan.
- Set layout to multi-planar: `set_layout(layout="all")`.
- Call `auto_window_level()` or set soft tissue windowing (`set_window_level(window=400, level=40)` for CT soft tissue, `window=1600, level=400` for CT bone).

### 2. Define Structure Palette (OARs & Target Volumes)
Define clinical structures with distinct high-visibility colors:
- **Target Volume (PTV/GTV)**: `create_label(label=1, name="Target Volume")` + `set_label_color(label=1, r=255, g=0, b=0)` (Red).
- **Organ at Risk 1 (Bladder / Brainstem)**: `create_label(label=2, name="OAR_Bladder")` + `set_label_color(label=2, r=255, g=255, b=0)` (Yellow).
- **Organ at Risk 2 (Rectum / Optic Chiasm)**: `create_label(label=3, name="OAR_Rectum")` + `set_label_color(label=3, r=0, g=255, b=0)` (Green).
- **Organ at Risk 3 (Femoral Heads / Parotids)**: `create_label(label=4, name="OAR_Femoral_L")` + `set_label_color(label=4, r=0, g=255, b=255)` (Cyan).

### 3. Perform Structure Contouring
- For homogenous density organs (e.g. bladder, lungs), use `threshold_segment(lower=..., upper=..., label=2, name="OAR_Bladder")`.
- For complex target structures, position the cursor using `move_cursor(x=..., y=..., z=...)` and evolve an active contour snake via `active_contour_segment(lower=..., upper=..., label=1, iterations=50)`.

### 4. Slice Interpolation & Boundary Smoothing
- For sparse slice contours, interpolate missing 2D slices across 3D using `interpolate_labels()`.
- Smooth organ boundaries using `smooth_labels(iterations=4)` to ensure smooth surfaces for dose calculation grids.
- Rebuild 3D mesh rendering: `update_3d_mesh()`.

### 5. Compute Structure Volumetry & Export Treatment Planning Surfaces
- Measure target volume ($cm^3$ / $cc$) using `get_label_stats(label=1)`.
- Export 3D surface meshes for radiation treatment planning:
  - `export_3d_mesh(label=1, path="PTV_Target.vtk")`
  - `export_3d_mesh(label=2, path="OAR_Bladder.vtk")`
  - `export_3d_mesh(label=3, path="OAR_Rectum.vtk")`
- Save complete radiation planning workspace: `save_workspace(path="radtx_plan_session.itksnap")`.
- Export structure set statistics: `save_statistics(path="radtx_structure_volumes.csv")`.
