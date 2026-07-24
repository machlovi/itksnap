"""Session state + the tool registry the agent can call.

In the MVP these tools operate on an in-memory SimpleITK image and a label
map. When this brain is embedded inside ITK-SNAP, the same tool *names* and
*args* stay the same, but their bodies get replaced by QWebChannel calls into
ITK-SNAP's live viewer. That is the whole point: the agent contract does not
change, only the backend that fulfils it.
"""
from __future__ import annotations

import io
import numpy as np
import SimpleITK as sitk
from PIL import Image


AXES = {"axial": 0, "z": 0, "coronal": 1, "y": 1, "sagittal": 2, "x": 2}


class Session:
    """Holds the current image + labels for one chat session."""

    def __init__(self, sid: str):
        self.sid = sid
        self.image: sitk.Image | None = None      # grayscale volume
        self.labels: sitk.Image | None = None     # uint16 label map
        self.label_names: dict[int, str] = {}     # id -> human name

    # -- helpers ---------------------------------------------------------
    def require_image(self) -> sitk.Image:
        if self.image is None:
            raise ToolError("No image is loaded yet. Load one first (try 'load demo').")
        return self.image

    def ensure_labels(self) -> sitk.Image:
        if self.labels is None:
            self.labels = sitk.Image(self.image.GetSize(), sitk.sitkUInt16)
            self.labels.CopyInformation(self.image)
        return self.labels


class ToolError(Exception):
    """Raised by a tool to send a clean error back to the agent."""


# ----------------------------------------------------------------------
# Tool implementations. Each returns a short dict "observation" that the
# agent (and the model) reads to decide the next step. Set "viewer": True
# when the image/labels changed so the UI refreshes the slice.
# ----------------------------------------------------------------------

def load_demo(session: Session) -> dict:
    """Create a synthetic phantom so the app is testable with no data files."""
    n = 128
    zz, yy, xx = np.mgrid[0:n, 0:n, 0:n]
    c = n // 2
    arr = np.full((n, n, n), 40, dtype=np.float32)                 # background
    organ = (xx - c) ** 2 + (yy - c) ** 2 + (zz - c) ** 2 < 45 ** 2
    arr[organ] = 100                                                # "organ"
    nodule = (xx - c - 15) ** 2 + (yy - c + 10) ** 2 + (zz - c) ** 2 < 9 ** 2
    arr[nodule] = 220                                               # bright "nodule"
    arr += np.random.normal(0, 4, arr.shape).astype(np.float32)     # mild noise
    img = sitk.GetImageFromArray(arr)
    img.SetSpacing((1.0, 1.0, 1.0))
    session.image = img
    session.labels = None
    session.label_names = {}
    return {"ok": True, "message": "Loaded a 128x128x128 demo phantom (background, organ, bright nodule).",
            "size": list(img.GetSize()), "viewer": True}


def load_image(session: Session, path: str) -> dict:
    """Load a real NIfTI/DICOM/MHA volume from disk."""
    try:
        img = sitk.ReadImage(path)
    except Exception as e:  # noqa: BLE001
        raise ToolError(f"Could not read '{path}': {e}")
    if img.GetNumberOfComponentsPerPixel() > 1:
        img = sitk.VectorIndexSelectionCast(img, 0)
    session.image = sitk.Cast(img, sitk.sitkFloat32)
    session.labels = None
    session.label_names = {}
    return {"ok": True, "message": f"Loaded {path}", "size": list(img.GetSize()),
            "spacing": [round(s, 3) for s in img.GetSpacing()], "viewer": True}


def threshold_segment(session: Session, lower: float, upper: float | None = None,
                      label: int = 1, name: str = "segmentation") -> dict:
    """Segment voxels whose intensity is in [lower, upper] into a label.

    This is a stand-in for a promptable/AI model. In ITK-SNAP it becomes a
    call to nnInteractive / SAM2 / a text-promptable model instead.
    """
    img = session.require_image()
    arr = sitk.GetArrayFromImage(img)
    hi = float(upper) if upper is not None else float(arr.max())
    mask = ((arr >= float(lower)) & (arr <= hi)).astype(np.uint16) * int(label)
    labels = session.ensure_labels()
    lab_arr = sitk.GetArrayFromImage(labels)
    lab_arr[mask > 0] = int(label)
    new_labels = sitk.GetImageFromArray(lab_arr)
    new_labels.CopyInformation(img)
    session.labels = new_labels
    session.label_names[int(label)] = name
    voxels = int((lab_arr == int(label)).sum())
    return {"ok": True, "message": f"Segmented '{name}' as label {label}.",
            "label": int(label), "voxels": voxels, "viewer": True}


def compute_volume(session: Session, label: int = 1) -> dict:
    """Volume of a label in millilitres (voxel count x spacing)."""
    if session.labels is None:
        raise ToolError("No segmentation exists yet.")
    lab_arr = sitk.GetArrayFromImage(session.labels)
    voxels = int((lab_arr == int(label)).sum())
    sx, sy, sz = session.labels.GetSpacing()
    ml = voxels * sx * sy * sz / 1000.0
    name = session.label_names.get(int(label), f"label {label}")
    return {"ok": True, "label": int(label), "name": name,
            "voxels": voxels, "volume_ml": round(ml, 2),
            "message": f"{name}: {round(ml, 2)} mL ({voxels} voxels)."}


def list_labels(session: Session) -> dict:
    """List labels currently present in the segmentation."""
    if session.labels is None:
        return {"ok": True, "labels": [], "message": "No segmentation yet."}
    lab_arr = sitk.GetArrayFromImage(session.labels)
    ids = [int(i) for i in np.unique(lab_arr) if i != 0]
    return {"ok": True,
            "labels": [{"id": i, "name": session.label_names.get(i, f"label {i}")} for i in ids],
            "message": f"{len(ids)} label(s) present."}


def show_slice(session: Session, axis: str = "axial", index: int | None = None) -> dict:
    """Focus the viewer on a slice (the UI then renders it live)."""
    img = session.require_image()
    ax = AXES.get(str(axis).lower(), 0)
    dim = img.GetSize()[::-1][ax]  # numpy axis length
    idx = dim // 2 if index is None else max(0, min(int(index), dim - 1))
    return {"ok": True, "axis": axis, "index": idx, "message": f"Showing {axis} slice {idx}.",
            "viewer": True, "focus": {"axis": axis, "index": idx}}


# ----------------------------------------------------------------------
# Registry: name -> {description, input_schema (JSON schema), fn}
# input_schema follows the same Anthropic-style shape RadAssistant sends, so
# the OpenAI-compatible backend forwards it verbatim as the function schema.
# ----------------------------------------------------------------------
TOOLS: dict[str, dict] = {
    "load_demo": {
        "description": "Load a built-in synthetic phantom volume. Use when the user has no data to test with.",
        "input_schema": {"type": "object", "properties": {}},
        "fn": load_demo,
    },
    "load_image": {
        "description": "Load a medical image volume from a file path (NIfTI/DICOM/MHA).",
        "input_schema": {"type": "object", "properties": {
            "path": {"type": "string", "description": "Absolute path to the image file."}},
            "required": ["path"]},
        "fn": load_image,
    },
    "threshold_segment": {
        "description": ("Segment voxels whose intensity falls in [lower, upper] into a label. "
                        "A stand-in for an AI/promptable model; becomes nnInteractive/SAM inside ITK-SNAP."),
        "input_schema": {"type": "object", "properties": {
            "lower": {"type": "number", "description": "Minimum intensity."},
            "upper": {"type": "number", "description": "Maximum intensity (optional; defaults to max)."},
            "label": {"type": "integer", "description": "Label id to write (default 1)."},
            "name": {"type": "string", "description": "Human name for the structure."}},
            "required": ["lower"]},
        "fn": threshold_segment,
    },
    "compute_volume": {
        "description": "Compute the volume in millilitres of a segmentation label.",
        "input_schema": {"type": "object", "properties": {
            "label": {"type": "integer", "description": "Label id (default 1)."}}},
        "fn": compute_volume,
    },
    "list_labels": {
        "description": "List the labels currently present in the segmentation.",
        "input_schema": {"type": "object", "properties": {}},
        "fn": list_labels,
    },
    "show_slice": {
        "description": "Point the viewer at a specific slice so the user can see it.",
        "input_schema": {"type": "object", "properties": {
            "axis": {"type": "string", "enum": ["axial", "coronal", "sagittal"]},
            "index": {"type": "integer", "description": "Slice index (optional; defaults to middle)."}}},
        "fn": show_slice,
    },
}


def tool_definitions() -> list[dict]:
    """The tool schemas sent to the model (name/description/input_schema)."""
    return [{"name": n, "description": s["description"], "input_schema": s["input_schema"]}
            for n, s in TOOLS.items()]


def run_tool(session: Session, name: str, args: dict) -> dict:
    spec = TOOLS.get(name)
    if not spec:
        raise ToolError(f"Unknown tool '{name}'. Available: {', '.join(TOOLS)}")
    return spec["fn"](session, **(args or {}))


# ----------------------------------------------------------------------
# Slice rendering for the live viewer (PNG bytes).
# ----------------------------------------------------------------------

def render_png(session: Session, axis: str = "axial", index: int | None = None,
               overlay: bool = True) -> bytes:
    img = session.require_image()
    vol = sitk.GetArrayFromImage(img)               # (z, y, x)
    ax = AXES.get(str(axis).lower(), 0)
    dim = vol.shape[ax]
    idx = dim // 2 if index is None else max(0, min(int(index), dim - 1))
    sl = np.take(vol, idx, axis=ax)

    # window/level normalize to 0..255
    lo, hi = np.percentile(sl, 1), np.percentile(sl, 99)
    hi = hi if hi > lo else lo + 1
    g = np.clip((sl - lo) / (hi - lo), 0, 1)
    rgb = np.stack([g, g, g], axis=-1)

    if overlay and session.labels is not None:
        lab = np.take(sitk.GetArrayFromImage(session.labels), idx, axis=ax)
        colors = [(0.94, 0.28, 0.24), (0.20, 0.75, 0.55), (0.30, 0.55, 0.95),
                  (0.96, 0.72, 0.20), (0.70, 0.40, 0.85)]
        for i, lid in enumerate([v for v in np.unique(lab) if v != 0]):
            m = lab == lid
            col = np.array(colors[i % len(colors)])
            rgb[m] = 0.45 * rgb[m] + 0.55 * col

    im = Image.fromarray((rgb * 255).astype(np.uint8))
    im = im.resize((im.width * 3, im.height * 3), Image.NEAREST)  # phantom is small
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()
