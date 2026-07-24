"""Entry point:  python -m server

Puts the server/ directory on sys.path so the flat imports (config, tools,
llm, agent, app) resolve, then launches uvicorn.
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import uvicorn  # noqa: E402
from config import config  # noqa: E402

if __name__ == "__main__":
    print(f"ITK-SNAP Agent MVP  |  LLM backend = {config.LLM}")
    print(f"Open  http://{config.HOST}:{config.PORT}/  in your browser.")
    uvicorn.run("app:app", host=config.HOST, port=config.PORT,
                app_dir=str(HERE), reload=False)
