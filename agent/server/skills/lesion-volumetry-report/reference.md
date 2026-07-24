# Volumetry report reference — what to capture, and how to present it

## What ITK-SNAP's per-label statistics contain
`Segmentation → Volumes and Statistics` (what `save_statistics` writes) reports, per
label: **label id, name, voxel count, volume (mm³ / mL), and intensity statistics
(mean, standard deviation)** of the underlying image inside that label, per image
layer. That is exactly the table a radiomics/reporting pipeline expects.

## A good saved report includes
1. **Structure name** (not "Label 1"). Rename before measuring.
2. **Volume in mL** (primary number) and **voxel count** (reproducibility check —
   voxel count × voxel volume = mL, so it lets someone re-derive the number).
3. **Method + parameters**: threshold band `[lower, upper]`, or snake band + seed +
   iterations. Reproducibility requires the parameters, not just the answer.
4. **Intensity stats** (mean ± SD inside the label) — meaningful for characterising a
   lesion (e.g. enhancing vs necrotic), included automatically by `save_statistics`.
5. **A snapshot** (`export_slice` PNG) centred on the structure.
6. **The workspace** (`.itksnap`) so the whole thing reopens.

## CSV conventions
- Prefer a `.csv` extension so it opens in Excel/pandas.
- One row per label; columns: `label_id, name, voxels, volume_mL, mean_intensity, sd_intensity`.
- Keep file names self-describing and free of spaces: `subj01_tumor_stats.csv`,
  `subj01_lesion_mask` workspace, `subj01_tumor_axial.png`.

## Longitudinal / multi-structure reporting
- For follow-up tracking, save one stats file per timepoint with the timepoint in the
  name (`tumor_baseline_stats.csv`, `tumor_month3_stats.csv`) and report the **absolute
  and percent volume change**.
- For multiple structures measured together, use `measure_all_labels` then
  `save_statistics` once — it writes every non-empty label in one table.

## Units cheat-sheet
- 1 mL = 1 cm³ = 1000 mm³. ITK-SNAP volumes are voxel_count × voxel_volume_mm³.
- If voxels are anisotropic (e.g. 0.5×0.5×3 mm), the tool already accounts for the
  true voxel volume — do not recompute from an assumed isotropic spacing.

## Sanity ranges (order-of-magnitude gut-check before saving)
- Adult kidney ≈ 120–200 mL; liver ≈ 1200–1800 mL; whole brain ≈ 1100–1500 mL.
- A "tumor" measuring 0.001 mL or 5000 mL is almost certainly a mis-seed or a leak —
  re-verify before writing the report.
