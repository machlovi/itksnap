---
name: longitudinal-followup-volumetry
description: Compare baseline vs follow-up longitudinal scan volumes, calculate percentage growth/shrinkage rate, and report lesion response.
---

# Longitudinal Follow-Up & Response Assessment Volumetry

This skill guides the quantitative comparison of lesion or structure volumes between baseline and follow-up scan timepoints to assess treatment response (e.g. RECIST/RANO criteria, tumor progression/regression).

---

## Step-by-Step Procedure

### 1. Load Scans & Align Timepoints
- Call `load_image(path="...")` to load the current follow-up scan.
- Load the co-registered baseline scan as an overlay: `load_overlay(path="...")`.
- Set layout to multi-view: `set_layout(layout="all")`.

### 2. Measure Follow-Up Lesion Volume
- Load or compute the follow-up lesion segmentation mask: `load_segmentation(path="...")` or `threshold_segment(...)`.
- Execute `get_label_stats(label=1)` to obtain the follow-up volume ($V_{followup}$) in $mL$ ($cm^3$).

### 3. Compute Delta & Growth/Regression Rate
- Calculate volume difference: $\Delta V = V_{followup} - V_{baseline}$.
- Calculate percentage change: $\% \text{ Change} = \frac{V_{followup} - V_{baseline}}{V_{baseline}} \times 100\%$.
- Classify response category:
  - **Complete Response (CR)**: 100% disappearance ($V_{followup} = 0$).
  - **Partial Response (PR)**: $\ge 30\%$ volume reduction.
  - **Progressive Disease (PD)**: $\ge 20\%$ volume increase.
  - **Stable Disease (SD)**: $< 20\%$ increase and $< 30\%$ decrease.

### 4. Export Handoff & Comparison Artifacts
- Focus view on lesion centroid using `focus_label(label=1)`.
- Export representative high-contrast slice snapshot: `export_slice(axis="axial", path="followup_comparison_slice.png")`.
- Export statistical summary to CSV: `save_statistics(path="longitudinal_response_summary.csv")`.
- Save complete follow-up session workspace: `save_workspace(path="longitudinal_followup.itksnap")`.
