---
name: brain-tumor-multimodal-workup
description: Perform multi-modal MRI brain tumor and glioma segmentation (T1, T2, FLAIR, T1+C) into enhancing tumor, necrotic core, and edema.
---

# Multi-Modal Brain Tumor & Glioma Segmentation Workup

This skill guides you through evaluating multi-contrast brain MRI series (T1-weighted, T2-weighted, FLAIR, and T1+Contrast) to segment brain tumors into standard clinical sub-regions:
- **Label 1: Enhancing Tumor (ET)** — Bright ring/region on T1+Contrast.
- **Label 2: Necrotic Core / Non-enhancing Tumor (NCR/NET)** — Hypointense core on T1+C.
- **Label 3: Peritumoral Edema / Hyperintensity (ED)** — Hyperintense region on T2/FLAIR.

---

## Step-by-Step Procedure

### 1. Load Primary & Overlay Modalities
- If the main image is not loaded, call `load_image(path="...")` for the T1+C or T1 scan.
- Load co-registered FLAIR and T2 scans as overlays using `load_overlay(path="...")`.
- Adjust initial view layout using `set_layout(layout="all")` to see Axial, Coronal, Sagittal, and 3D.

### 2. Prepare Display & Contrast
- Call `auto_window_level()` to fit the intensity range for tumor visibility.
- Set window/level for high-contrast tumor margin visualization: `set_window_level(window=600, level=300)`.
- Set segmentation opacity: `set_segmentation_opacity(opacity=45)`.

### 3. Define Clinical Labels
- Create Label 1 (Enhancing Tumor): `create_label(label=1, name="Enhancing Tumor")` + `set_label_color(label=1, r=255, g=0, b=0)` (Red).
- Create Label 2 (Necrotic Core): `create_label(label=2, name="Necrotic Core")` + `set_label_color(label=2, r=255, g=255, b=0)` (Yellow).
- Create Label 3 (Peritumoral Edema): `create_label(label=3, name="Edema")` + `set_label_color(label=3, r=0, g=255, b=0)` (Green).

### 4. Segment Sub-Regions
- **Enhancing Tumor**: Use `threshold_segment(lower=180, upper=500, label=1, name="Enhancing Tumor")`.
- **Necrotic Core**: Focus inside the tumor ring using `move_cursor(x=..., y=..., z=...)` and segment hypointense core into Label 2.
- **Edema**: Use FLAIR intensity thresholding with `threshold_segment(lower=120, upper=240, label=3, name="Edema")`.

### 5. Smooth & Refine Boundaries
- Call `smooth_labels(iterations=3)` to eliminate staircasing artifacts on 3D boundaries.
- Rebuild 3D mesh representation using `update_3d_mesh()`.

### 6. Extract Quantitative Tumor Radiomics & Statistics
- Call `get_label_stats(label=1)` for Enhancing Tumor volume and intensity stats.
- Call `get_label_stats(label=2)` for Necrotic Core.
- Call `get_label_stats(label=3)` for Edema.
- Export statistics summary to disk: `save_statistics(path="tumor_volumetry_report.csv")`.
- Export session workspace: `save_workspace(path="brain_tumor_session.itksnap")`.
