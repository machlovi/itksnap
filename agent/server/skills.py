"""Skills, the Claude way: one directory per skill (``skills/<name>/SKILL.md``)
with ``name`` + ``description`` frontmatter, loaded ON DEMAND, with optional
bundled reference files for progressive disclosure.

Three levels of disclosure, exactly like Anthropic Agent Skills:
  1. INDEX  — name + one-line description of every skill, injected into the
              system prompt. Always in context; this is the trigger.
  2. BODY   — the full SKILL.md procedure, returned only when the model calls
              ``use_skill(name)`` because a request matched the description.
  3. FILES  — deep reference material (``reference.md``, threshold tables, worked
              examples) living next to SKILL.md, pulled in only when the body
              tells the model to call ``read_skill_file(name, file)``.

Keeping bodies out of the prompt until needed is what lets each skill be long
and richly detailed without bloating every request.
"""
from __future__ import annotations
import os, glob

SKILLS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "skills")


def _parse_frontmatter(text, fallback_name):
    """Return (name, description, body) from a SKILL.md with a --- ... --- header."""
    name, desc, body = fallback_name, "", text
    if text.startswith("---"):
        end = text.find("---", 3)
        if end != -1:
            fm, body = text[3:end], text[end + 3:].lstrip("\n")
            key = None
            for line in fm.strip().splitlines():
                # fold indented continuation lines of a multi-line YAML scalar
                if line[:1] in (" ", "\t") and key == "description":
                    desc = (desc + " " + line.strip()).strip()
                    continue
                if ":" in line:
                    k, v = line.split(":", 1)
                    k, v = k.strip(), v.strip()
                    key = k
                    if k == "name" and v:
                        name = v
                    elif k == "description":
                        # '>' (folded) / '|' (literal) block scalar: body is on the
                        # following indented lines, not this one.
                        desc = "" if v in (">", "|", ">-", "|-", ">+", "|+") else v
    return name, desc.strip(), body.strip()


def _discover():
    """Find every skill directory (skills/<name>/SKILL.md), plus legacy flat
    skills/<name>.md files for back-compat. Directory form wins on name clash."""
    out = {}
    # legacy flat files first (so directory form can override)
    for p in sorted(glob.glob(os.path.join(SKILLS_DIR, "*.md"))):
        text = open(p, encoding="utf-8").read()
        fallback = os.path.splitext(os.path.basename(p))[0]
        name, desc, body = _parse_frontmatter(text, fallback)
        out[name] = {"name": name, "description": desc, "body": body,
                     "dir": None, "files": []}
    # canonical directory-per-skill form
    for skill_md in sorted(glob.glob(os.path.join(SKILLS_DIR, "*", "SKILL.md"))):
        d = os.path.dirname(skill_md)
        text = open(skill_md, encoding="utf-8").read()
        fallback = os.path.basename(d)
        name, desc, body = _parse_frontmatter(text, fallback)
        files = sorted(
            os.path.basename(f) for f in glob.glob(os.path.join(d, "*"))
            if os.path.isfile(f) and os.path.basename(f) != "SKILL.md"
        )
        out[name] = {"name": name, "description": desc, "body": body,
                     "dir": d, "files": files}
    return out


_SKILLS = _discover()


def skills_index_block():
    """The lightweight index injected into the system prompt (names + descriptions)."""
    if not _SKILLS:
        return ""
    lines = ["", "AVAILABLE SKILLS (multi-step expert procedures). You see only their names "
             "and descriptions here. When a request matches one, call the `use_skill` tool with "
             "its name to load the full step-by-step procedure, then follow it exactly:"]
    for s in _SKILLS.values():
        lines.append(f"- {s['name']}: {s['description']}")
    return "\n".join(lines)


def get_skill(name):
    """Return (body, error). The body has a trailing note listing any bundled
    reference files the model may load next with read_skill_file."""
    s = _SKILLS.get((name or "").strip())
    if not s:
        avail = ", ".join(_SKILLS.keys())
        return None, f"No skill named '{name}'. Available skills: {avail}."
    body = s["body"]
    if s["files"]:
        flist = ", ".join(s["files"])
        body += ("\n\n---\nReference files bundled with this skill (load only if a step above "
                 f"tells you to, via read_skill_file): {flist}")
    return body, None


# --- Reliable server-side matching (progressive disclosure that works even on
#     small local models that won't call use_skill themselves) ---------------
# Curated trigger phrases per skill. The matcher injects ONE matched skill's body
# into the turn's system prompt; use_skill stays available for capable models and
# for loading a different skill on demand.
_TRIGGERS = {
    "active-contour-segmentation": [
        "active contour", "active-contour", "snake", "region grow", "region-grow",
        "region growing", "region competition", "level set", "level-set", "grow the",
        "grow a", "grow this", "evolve", "bubble", "seed"],
    "brain-tumor-multimodal-workup": [
        "brain tumor", "glioma", "glioblastoma", "tumor ring", "necrotic", "edema",
        "flair", "t1+c", "t1c", "enhancing tumor", "tumor workup", "lesion workup",
        "ncr", "net", "et", "ed"],
    "radiation-therapy-oar-contouring": [
        "radiation", "radiotherapy", "oar", "organ at risk", "gtv", "ctv", "ptv",
        "target volume", "contouring", "treatment planning", "vtk mesh", "mesh export",
        "bladder", "rectum", "prostate", "brainstem"],
    "radiomics-intensity-stats": [
        "radiomics", "intensity stats", "mean intensity", "stddev", "min max",
        "signal intensity", "hu value", "hounsfield", "quantitative stats",
        "histogram stats", "intensity range"],
    "longitudinal-followup-volumetry": [
        "longitudinal", "follow-up", "followup", "baseline vs", "growth rate",
        "percentage change", "recist", "rano", "progression", "regression",
        "shrinkage", "timepoints", "over time"],
    "multi-structure-segmentation": [
        "multiple", "several", "each label", "separate label", "separate labels",
        "two tissue", "structures", "and the spleen", "both kidneys", "organs",
        "multi-label", "multi label", "several structures", "different labels"],
    "segmentation-cleanup": [
        "smooth", "clean up", "cleanup", "refine", "jagged", "staircase",
        "fill the gap", "fill gaps", "interpolat", "islands", "leak", "tidy",
        "polish", "de-stair", "fix the mask", "fix the segmentation"],
    "prepare-display": [
        "contrast", "window", "brightness", "too dark", "too bright", "washed out",
        "opacity", "transparen", "layout", "axial view", "coronal view",
        "sagittal view", "can't see", "cannot see", "hard to see", "make it visible"],
    "multimodal-overlay-compare": [
        "overlay", "compare", "baseline", "follow-up", "followup", "modalit",
        "t1", "t2", "flair", "another scan", "second scan", "two scans",
        "longitudinal", "timepoint", "over time"],
    "export-results": [
        "save everything", "export", "back up", "backup", "save the workspace",
        "write to disk", "hand off", "handoff", "save all", "save my",
        "workspace", "label description", "descriptions"],
}


def match_skill(user_text):
    """Return the single best-matching skill name for a request, or None.

    Requires the top score to be >=1 and strictly greater than the runner-up
    (a tie means ambiguous → don't inject; let the model decide / use_skill)."""
    text = (user_text or "").lower()
    scored = []
    for name in _SKILLS:
        phrases = _TRIGGERS.get(name, [])
        score = sum(1 for p in phrases if p in text)
        if score:
            scored.append((score, name))
    if not scored:
        return None
    scored.sort(reverse=True)
    if len(scored) > 1 and scored[0][0] == scored[1][0]:
        return None  # ambiguous
    return scored[0][1]


def read_skill_file(name, filename):
    """Return (content, error) for a bundled reference file of a skill."""
    s = _SKILLS.get((name or "").strip())
    if not s or not s.get("dir"):
        return None, f"No skill named '{name}' with reference files."
    safe = os.path.basename((filename or "").strip())  # no path traversal
    if safe not in s["files"]:
        avail = ", ".join(s["files"]) or "(none)"
        return None, f"Skill '{name}' has no file '{filename}'. Available: {avail}."
    path = os.path.join(s["dir"], safe)
    try:
        return open(path, encoding="utf-8").read().strip(), None
    except OSError as e:
        return None, f"Could not read '{safe}': {e}"


# the synthetic server-side tools the model uses to pull skills in on demand
USE_SKILL_TOOL = {
    "name": "use_skill",
    "description": "Load the full step-by-step procedure for one of the AVAILABLE SKILLS listed "
                   "in the system prompt, so you can follow it. Call this as soon as the user's "
                   "request matches a skill's description, BEFORE acting.",
    "input_schema": {"type": "object",
                     "properties": {"name": {"type": "string", "description": "The skill name to load."}},
                     "required": ["name"]},
}

READ_SKILL_FILE_TOOL = {
    "name": "read_skill_file",
    "description": "Load a bundled reference file for a skill (deep detail: parameter tables, "
                   "worked examples, edge cases). Only call this when the skill body you already "
                   "loaded explicitly points you to the file.",
    "input_schema": {"type": "object",
                     "properties": {
                         "name": {"type": "string", "description": "The skill name."},
                         "file": {"type": "string", "description": "The reference file name, e.g. reference.md."}},
                     "required": ["name", "file"]},
}

SKILL_TOOLS = [USE_SKILL_TOOL, READ_SKILL_FILE_TOOL]
