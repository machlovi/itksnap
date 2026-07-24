# Window/Level reference — CT presets and MR guidance

`set_window_level(window, level)`: **Level** = window centre (brightness), **Window** =
window width (contrast). Both in the image's intensity units.

## CT presets (Hounsfield Units) — standard radiology windows
CT is calibrated in HU, so these presets are absolute and reliable.

| Tissue / purpose | Window (width) | Level (centre) |
|---|---|---|
| Soft tissue / abdomen | 400 | 40 |
| Brain | 80 | 40 |
| Lung | 1500 | −600 |
| Bone | 1500–2000 | 300–400 |
| Mediastinum | 350 | 50 |
| Liver (narrow) | 150 | 60 |
| Stroke / posterior fossa (narrow brain) | 30–40 | 35 |
| Angio / contrast vessels | 600–700 | 100–150 |

Rule of thumb: to reveal a structure at HU value `V`, set **Level ≈ V** and pick a
**Window** roughly 2–8× the contrast difference you want to see across.

## MR (and other non-calibrated modalities)
MR intensities are **not** absolute (they vary by scanner/sequence/scaling), so there
are no universal presets. Use `auto_window_level` first (robust percentile mapping),
then nudge:
- Structure too dark → **lower the Level**.
- Not enough contrast between two tissues → **narrow the Window** around their
  intensity.
- To find good numbers, read a voxel inside the target with `get_cursor_info` /
  `get_scene_overview` and centre the Level near that intensity.

## Quick intents → action
- "It's too dark / washed out / fix contrast" → `auto_window_level`.
- "Show me a lung/bone/brain window" (CT) → `set_window_level` with the preset above.
- "More contrast" → narrow Window (smaller number).
- "Brighter" → lower Level.
- "It went too far / undo the contrast" → `auto_window_level` to reset to a sane range.
