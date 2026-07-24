"""Runtime configuration, read from environment variables.

Defaults to the dependency-free mock so the app runs with zero setup. To do
LIVE testing against your cluster-hosted model, point it at an OpenAI-compatible
endpoint (vLLM / TGI / llama.cpp / LM Studio / Ollama's /v1 shim) — typically a
localhost port-forward to the cluster:

    $env:AGENT_LLM   = "openai"
    $env:LLM_BASE_URL= "http://localhost:8000/v1"   # SSH-tunnelled to the cluster
    $env:LLM_MODEL   = "Qwen2.5-7B-Instruct"
    $env:LLM_API_KEY = ""                            # if your server requires one

You can also set these at runtime from the web UI (POST /api/llm) — no restart.
"""
import os


class Config:
    # Backend: mock | openai   (openai == any OpenAI-compatible /v1 server)
    LLM = os.environ.get("AGENT_LLM", "mock").lower()

    # OpenAI-compatible endpoint (this is how a cluster model is reached)
    LLM_BASE_URL = os.environ.get("LLM_BASE_URL", "http://localhost:8000/v1")
    LLM_MODEL = os.environ.get("LLM_MODEL", "")
    LLM_API_KEY = os.environ.get("LLM_API_KEY", "")
    LLM_TIMEOUT = int(os.environ.get("LLM_TIMEOUT", "600"))

    # Server
    HOST = os.environ.get("AGENT_HOST", "127.0.0.1")
    PORT = int(os.environ.get("AGENT_PORT", "8077"))

    # Safety cap on tool-calling rounds per user message (raised for long,
    # multi-step tasks; still a backstop against runaway loops)
    MAX_ROUNDS = int(os.environ.get("AGENT_MAX_ROUNDS", "100"))


config = Config()
