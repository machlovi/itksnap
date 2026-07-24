# ITK-SNAP AI Agent Sidecar (`agent/`)

A high-performance, tool-calling AI agent sidecar for **ITK-SNAP**. It bridges natural language prompts from doctors and researchers to ITK-SNAP's C++ image analysis engine (`SNAPRemoteControl`) over a local WebSocket connection (`ws://127.0.0.1:8077`).

---

## 🏗️ Architecture & LLM Design Patterns

The agent is designed following industry-standard LLM agent design patterns (OpenAI Tool Calling Spec & Anthropic Agent Skills Architecture):

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                            ITK-SNAP Desktop App                             │
 │   ┌───────────────────────┐               ┌─────────────────────────────┐   │
 │   │    AssistantPanel     │               │      SNAPRemoteControl      │   │
 │   │   (Qt WebSocket UI)   │──────────────►│    (C++ RPC Command Engine) │   │
 │   └───────────┬───────────┘               └──────────────┬──────────────┘   │
 └───────────────┼──────────────────────────────────────────┼──────────────────┘
                 │ WebSocket (`ws://127.0.0.1:8077`)        │ 65+ Tool Executions
                 ▼                                          ▼
 ┌───────────────────────────────┐           ┌─────────────────────────────┐
 │       itksnap-agent           │           │       IRISApplication       │
 │    (FastAPI / WebSockets)     │           │      (Core Image Engine)    │
 └───────────────┬───────────────┘           └─────────────────────────────┘
                 │ OpenAI-compatible / Anthropic API
                 ▼
 ┌───────────────────────────────┐
 │     Local or Cloud LLM        │
 │  (Qwen, Llama3, Ollama, etc.) │
 └───────────────────────────────┘
```

---

## 🧠 Real-Time Reasoning & Thought Streaming
The agent extracts reasoning steps (`reasoning_content` or `thinking_delta`) from advanced LLMs (such as DeepSeek R1 or Anthropic Claude) and streams them in real-time. In the ITK-SNAP GUI, these show up inside a beautifully formatted **🧠 Model Reasoning** panel before the final answer or tool calls.

---

## 🛠️ Tool Calling & Skill System (LLM Best Practices)

### 1. Dynamic Tool Registration (`SNAPRemoteControl`)
Tool schemas are **not hardcoded in Python**. When ITK-SNAP connects to the agent server, it registers its complete suite of **65+ C++ tool schemas** dynamically via a JSON-RPC `hello` message:

```json
{
  "name": "threshold_segment",
  "description": "Segment voxels whose intensity falls within [lower, upper] range into a label.",
  "parameters": {
    "type": "object",
    "properties": {
      "lower": {"type": "number", "description": "Minimum intensity threshold"},
      "upper": {"type": "number", "description": "Maximum intensity threshold"},
      "label": {"type": "integer", "description": "Segmentation label ID"}
    },
    "required": ["lower"]
  }
}
```

### 2. Multi-Step Expert Skills (`skills/<name>/SKILL.md`)
Complex medical workflows (e.g. lesion volumetry reports, active contour snake segmentation, multi-structure labelling, noise cleanup) are structured as **Agent Skills**:

- **Directory Layout**: Each skill lives in `skills/<name>/SKILL.md`.
- **YAML Frontmatter Header**: Contains `name` and `description` triggers for LLM index matching.
- **Progressive Disclosure**:
  1. **Index**: Only skill names and descriptions are placed in the system prompt to keep context lightweight.
  2. **Body**: When a user's request matches a skill, the step-by-step markdown procedure is loaded on demand via `use_skill(name)` or automatic trigger matching (`match_skill`).
  3. **Reference Files**: Deep detail tables (`reference.md`) are loaded only when explicitly needed (`read_skill_file`).

```text
agent/server/skills/
 ├── active-contour-segmentation/
 │    ├── SKILL.md                 <-- Step-by-step procedure (YAML header + Markdown)
 │    └── reference.md             <-- Parameter tuning table
 ├── lesion-volumetry-report/
 │    ├── SKILL.md
 │    └── reference.md
 ├── multi-structure-segmentation/
 ├── prepare-display/
 └── segmentation-cleanup/
```

---

## 🚀 Running the Agent

### 1. Auto-Launch inside ITK-SNAP (Recommended)
When you launch `ITK-SNAP.exe`, ITK-SNAP automatically detects `agent/` or `itksnap-agent.exe`, **spawns the agent server silently in the background**, and establishes the WebSocket link automatically.

### 2. Manual Developer Startup (Optional)
If you want to run or debug the Python agent server independently:

```powershell
# Navigate to agent folder
cd agent

# Activate virtual environment
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Install dependencies
pip install -r requirements.txt

# Start agent server on port 8077
python -m server
```

---

## 🔗 Connecting Local LLM Endpoints

You can point the agent at any local or cloud LLM server directly from the ITK-SNAP Assistant panel or via environment variables:

| LLM Engine | LLM Endpoint URL | Model ID |
| :--- | :--- | :--- |
| **Local vLLM / llama.cpp** | `http://localhost:11445/v1` | `qwen` / `llama-3.1` |
| **Ollama** | `http://localhost:11434/v1` | `qwen2.5-coder` |
| **OpenAI / LMStudio** | `http://localhost:1234/v1` | `gpt-4o` |

---

## 📁 Folder Structure

```text
agent/
 ├── server/
 │    ├── __main__.py       <-- Entry point
 │    ├── app.py            <-- FastAPI & WebSocket route handlers
 │    ├── agent.py          <-- Core LLM tool-calling loop & context manager
 │    ├── llm.py            <-- OpenAI/Anthropic/Ollama backend connectors
 │    ├── tools.py          <-- Synthetic server tools
 │    └── skills.py         <-- Progressive disclosure skill engine
 ├── web/                   <-- Standalone Web UI (HTML/JS/CSS)
 ├── eval/                  <-- Ground-truth evaluation harness & test suites
 ├── test.md                <-- 10 Advanced Knee MRI clinical test tasks
 ├── requirements.txt       <-- Python package dependencies
 └── README.md              <-- Agent documentation
```
