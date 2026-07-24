# ITK-SNAP AI Agent Sidecar (`agent/`)

A high-performance, tool-calling AI agent sidecar for **ITK-SNAP**. It bridges natural language prompts from doctors and researchers to ITK-SNAP's C++ image analysis engine (`SNAPRemoteControl`) over a local WebSocket connection (`ws://127.0.0.1:8077/wsbridge`).

---

## 🏗️ Architecture & System Integration

The agent integrates into ITK-SNAP as a lightweight sidecar process, handling heavy language model inference while leaving the desktop GUI responsive:

```text
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                            ITK-SNAP Desktop App                             │
 │   ┌───────────────────────┐               ┌─────────────────────────────┐   │
 │   │    AssistantPanel     │               │      SNAPRemoteControl      │   │
 │   │   (Qt WebSocket UI)   │──────────────►│    (C++ RPC Command Engine) │   │
 │   └───────────┬───────────┘               └──────────────┬──────────────┘   │
 └───────────────┼──────────────────────────────────────────┼──────────────────┘
                 │ WebSocket (`ws://127.0.0.1:8077`)        │ 65+ Dynamic Tools
                 ▼                                          ▼
 ┌───────────────────────────────┐           ┌─────────────────────────────┐
 │       itksnap-agent           │           │       IRISApplication       │
 │    (FastAPI / WebSockets)     │           │      (Core Image Engine)    │
 └───────────────┬───────────────┘           └─────────────────────────────┘
                 │ OpenAI / Anthropic APIs
                 ▼
 ┌───────────────────────────────┐
 │     Local or Cloud LLM        │
 │  (Qwen, Llama3, Claude, etc.) │
 └───────────────────────────────┘
```

---

## 🧠 Real-Time Reasoning & Thought Streaming

To support reasoning-oriented LLMs (like DeepSeek R1, OpenAI o1/o3-mini, and Claude 3.7):
* **Streaming Protocol**: The Python agent server detects reasoning token generation (`reasoning_content` or `thinking_delta` chunks) and broadcasts them live as `type: "thought"` events.
* **Preserving Whitespace**: Streamed text is inserted via QTextCursor plain text streams in the Qt GUI, preserving spaces, formatting, and layout structure exactly as generated.
* **Brain Visualizer Box**: The Assistant Panel UI renders reasoning steps in real-time inside a dedicated purple accent card (**🧠 Model Reasoning**), auto-closing it cleanly when transitioning to final responses or tool calls.

---

## 🛠️ Tool Calling & dynamic RPC Schemas

### 1. Dynamic Tool Discovery
Instead of hardcoding tool signatures in Python, the C++ client dynamically registers its entire capabilities catalog on handshake. ITK-SNAP sends a `hello` message containing **65+ dynamic tool schemas** compiled from the core application:

```json
{
  "type": "hello",
  "tools": [
    {
      "name": "threshold_segment",
      "description": "Segment voxels whose intensity is in [lower, upper] into a label.",
      "input_schema": {
        "type": "object",
        "properties": {
          "lower": {"type": "number", "description": "Minimum intensity"},
          "upper": {"type": "number", "description": "Maximum intensity"},
          "label": {"type": "integer", "description": "Label ID"}
        },
        "required": ["lower"]
      }
    }
  ]
}
```

### 2. Multi-Step Expert Skills (`skills/`)
Complex radiological procedures (active contours, tumor segmentation cleanup, windowing presets) are organized as **Agent Skills** with progressive disclosure:
* **SKILL.md**: Frontmatter-indexed markdown procedure loaded on demand.
* **reference.md**: Context tables containing detailed parameters, loaded only when explicitly requested.

---

## 🚀 Getting Started

### 1. Auto-Launch inside ITK-SNAP
When ITK-SNAP starts up, it automatically checks the application directory for `agent/` or `itksnap-agent.exe` and launches the sidecar server in the background.

### 2. Manual Developer Startup (Optional)
If running independently for testing or debugging:

```powershell
# Navigate to agent directory
cd agent

# Create and activate virtual environment
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Install requirements
pip install -r requirements.txt

# Start the agent server on port 8077
python -m server
```

---

## 🔗 Supported LLM Configurations

You can configure the model source dynamically in the Assistant Panel or via environment variables:

| LLM Engine | LLM Endpoint URL | Model ID |
| :--- | :--- | :--- |
| **Local vLLM / llama.cpp** | `http://localhost:11445/v1` | `qwen` / `llama` |
| **Ollama** | `http://localhost:11434/v1` | `qwen2.5-coder` |
| **Anthropic Cloud** | `https://api.anthropic.com/v1` | `claude-3-5-sonnet-20241022` |

---

## 📁 Project Directory Layout

```text
agent/
 ├── server/
 │    ├── __main__.py       <-- Server entry point
 │    ├── app.py            <-- FastAPI server & WS route management
 │    ├── agent.py          <-- LangGraph React Graph & Native ReAct fallback
 │    ├── llm.py            <-- Low-level HTTP stream parser for reasoning
 │    ├── config.py         <-- Environment variable configurations
 │    ├── tools.py          <-- Server-side helper tools
 │    └── skills.py         <-- Progressive disclosure matching logic
 ├── web/                   <-- Standalone web client interface
 ├── eval/                  <-- Ground-truth evaluation harness
 ├── test.md                <-- 10 advanced Knee MRI clinical test tasks
 ├── requirements.txt       <-- Python dependency specifications
 └── README.md              <-- This documentation file
```
