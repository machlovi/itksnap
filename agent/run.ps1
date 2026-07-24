# Run the ITK-SNAP Agent MVP.
#   .\run.ps1                # mock LLM (no keys, no GPU) - great for first test
#   $env:AGENT_LLM="ollama"; .\run.ps1
#   $env:AGENT_LLM="anthropic"; $env:ANTHROPIC_API_KEY="sk-..."; .\run.ps1
param()
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
if (-not $env:AGENT_LLM) { $env:AGENT_LLM = "mock" }
python -m server
