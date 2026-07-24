# Embedding the Assistant inside ITK-SNAP

This folder is the **C++ integration scaffold** that puts the itksnap-agent chat
**inside ITK-SNAP** as a dockable panel that drives ITK-SNAP's own tools via LLM
calls. It is grounded in the real `pyushkevich/itksnap` master source (verified).

## Architecture (why this is thin)

```
  ┌─────────────────────────── ITK-SNAP (C++/Qt6, forked) ───────────────────────┐
  │  2D/3D viewer  ◄── masks/labels refresh via InvokeEvent(SegmentationChange)   │
  │  ┌── QDockWidget "Assistant" ─────────────────┐                               │
  │  │  AssistantPanel (native Qt Widgets)         │   executes tool calls on the  │
  │  │  QTextBrowser transcript + QLineEdit        │   GUI thread via              │
  │  │  QWebSocket  ──────────────┐                │   GlobalUIModel::GetDriver()  │
  │  └────────────────────────────┼───────────────┘        → IRISApplication      │
  └───────────────────────────────┼──────────────────────────────────────────────┘
                                   │  ws://127.0.0.1:8077/wsbridge  (tested protocol)
                                   ▼
        ┌──────────── itksnap-agent  (Python sidecar, already built + tested) ────┐
        │  agent loop + backend-agnostic LLM routing (your llama.cpp `localmodel`) │
        │  RemoteToolHost: forwards each tool_call to ITK-SNAP, awaits tool_result │
        └──────────────────────────────────────────────────────────────────────────┘
```

**Key point:** the LLM and the agent loop stay in the Python sidecar (already
working against your `localmodel`). ITK-SNAP only: (1) advertises its tools,
(2) renders chat events, (3) runs each tool call against `IRISApplication`.
That is why **no QtWebEngine and no QWebChannel are needed** — only
`Qt6::WebSockets`.

## The `/wsbridge` protocol (tested, deterministic)

```
client → {type:"hello", tools:[<schemas>]}          once, on connect
client → {type:"user",  text:"..."}                 each user message
server → {type:"status"|"token"|"assistant"|"error"|"turn_end", ...}   render these
server → {type:"tool_call", id, name, args}          → execute in ITK-SNAP
client → {type:"tool_result", id, ok, text}          → return the result
```
`AssistantPanel.cxx` implements the client side; `server/toolhost.py`
(`RemoteToolHost`) + `server/app.py` (`/wsbridge`) implement the server side.

## Tool → ITK-SNAP Logic mapping (all verified public API)

| Tool | ITK-SNAP call (via `GlobalUIModel::GetDriver()`) |
|------|--------------------------------------------------|
| `load_image(path)` | `IRISApplication::OpenImage(path, MAIN_ROLE, wl)` |
| `list_labels()` | `GetColorLabelTable()->GetValidLabels()` |
| `measure_volume(label)` | `SegmentationStatistics::Compute()` → `GetStats()[label].volume_mm3` |
| `move_cursor(x,y,z)` | `IRISApplication::SetCursorPosition(Vector3ui, true)` |
| `segment_structure(name)` | delegate to the existing `DeepLearningSegmentationModel` (nnInteractive) |
| refresh after a change | `GetDriver()->InvokeEvent(SegmentationChangeEvent())` |

## Files here
- `AssistantPanel.h` / `AssistantPanel.cxx` — the dock panel (copy into `GUI/Qt/Windows/`)
- `MainImageWindow.integration.diff` — how to add the dock (copy the `m_DockRight` pattern)
- `CMake.integration.diff` — add `Qt6::WebSockets` + the new sources

## How to build + run (once you have ITK-SNAP building)

ITK-SNAP master is **Qt6** and is **NOT a superbuild** — it `find_package`s
**prebuilt** ITK 5.4, VTK 9.3.1, Qt 6.8.1 (MSVC 2022). So:

1. Install **VS Build Tools 2022 → "Desktop development with C++"** workload.
2. Install **Qt 6.8.1** (msvc2022_64) + the `qtwebsockets` module (aqt or the online installer).
3. Get **prebuilt ITK 5.4** and **VTK 9.3.1** (build once, or use binaries).
4. Clone `pyushkevich/itksnap`, copy the two `AssistantPanel.*` files into `GUI/Qt/Windows/`,
   apply the `MainImageWindow` and `CMake` diffs.
5. CMake-configure pointing `ITK_DIR`, `VTK_DIR`, `Qt6_DIR` at your prebuilt deps; build.

**Run:**
1. Start the sidecar pointed at your model:
   ```powershell
   cd D:\itksnap-agent
   $env:AGENT_LLM="openai"; $env:LLM_BASE_URL="http://localhost:11440"; $env:LLM_MODEL="localmodel"
   .\run.ps1        # serves /wsbridge on 8077
   ```
2. Launch your built ITK-SNAP, open the **Assistant** dock (View menu).
   It connects to `ws://127.0.0.1:8077/wsbridge/itksnap`, registers its tools,
   and you can chat: *"load D:/data/ct.nii.gz, segment the left kidney, measure it."*

## Open items (honest)
- **`segment_structure`** is stubbed — it must be wired to the app's real
  `DeepLearningSegmentationModel` (nnInteractive). That model already exists and
  writes masks into the live viewer (`UpdateSegmentation`), so this is wiring,
  not new algorithm work.
- The exact `OpenImage` overload / a couple of include paths may need small
  in-tree adjustments (marked `TODO` in the .cxx).
- Config: the server URL is hard-coded to `:8077`; make it a preference.

## Status
- Sidecar `/wsbridge` protocol: **built and tested** (mock end-to-end; `localmodel`
  proven on the same agent path).
- C++ panel: **scaffold, compiles-shaped** against verified API; needs the ITK-SNAP
  build to compile in-tree and the `segment_structure` wiring.
