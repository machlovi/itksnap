---
name: radiomics-intensity-stats
description: Compute quantitative radiomics statistics (volume in mL/mm3, voxel count, mean intensity, stddev, min, max) for structures and export CSV spreadsheets.
---

# Quantitative Radiomics & Intensity Statistics Extraction

This skill extracts quantitative image biomarkers and radiomics intensity statistics for segmented structures across underlying medical image modalities (CT, MRI, PET).

---

## Step-by-Step Procedure

### 1. Verify Image & Segmentation Status
- Call `get_scene_overview()` to confirm the main image volume is loaded and identify all valid segmentation labels.
- If no segmentation is loaded, load the label mask using `load_segmentation(path="...")`.

### 2. Extract Per-Structure Quantitative Statistics
- For each target label of interest, invoke `get_label_stats(label=N)`.
- The tool returns:
  - Voxel count
  - Absolute volume in $mm^3$ and $mL$ ($cc$)
  - Mean signal/HU intensity across the ROI
  - Signal intensity standard deviation ($\sigma$)
  - Minimum and Maximum voxel intensity values

### 3. Comprehensive Multi-Label Measurement
- Call `measure_all_labels()` to extract volumes for every defined structure in the color label table.
- Call `cursor_info()` at peak intensity locations to inspect localized voxel values.

### 4. Export Statistical Report & Handoff Data
- Export the complete multi-label statistical spreadsheet to a CSV file for statistical software (R, Python pandas, SPSS):
  `save_statistics(path="radiomics_intensity_report.csv")`
- Save the current label descriptions color table:
  `save_labels(path="structure_descriptions.txt")`
- Summarize key findings in clear markdown table format for the clinician:
  - Label ID & Name
  - Volume ($mL$)
  - Mean Intensity ± StdDev
  - Intensity Range [Min - Max]
