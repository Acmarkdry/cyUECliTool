# Installation

UECliTool v0.6.0 is CLI-first. The normal path is:

```text
Codex or user -> ue.ps1 -> local daemon -> Unreal Editor TCP bridge
```

The legacy MCP server is still available, but it is no longer the default
model-facing entrypoint.

## Requirements

- Unreal Engine 5.6 or newer.
- Visual Studio 2022 for C++ builds on Windows.
- The project plugin at `Plugins/UEEditorMCP`.
- No external Python install is required when Unreal's bundled Python is
  available.

## Build The Plugin

Build the project editor target so Unreal compiles the plugin:

```powershell
D:\UnrealEngine5\UnrealEngine\Engine\Build\BatchFiles\Build.bat `
  LyraEditor Win64 Development `
  D:\UnrealGame\Lyra_56\Lyra_56.uproject -waitmutex
```

Open the editor with the configured bridge port:

```powershell
D:\UnrealEngine5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe `
  D:\UnrealGame\Lyra_56\Lyra_56.uproject -MCPPort=55558
```

The C++ bridge name still uses `MCPPort` for compatibility.

## Setup Python

From the plugin directory:

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\setup_mcp.ps1
```

The setup script:

1. Locates Unreal's bundled Python.
2. Creates `Python/.venv`.
3. Installs Python dependencies.
4. Writes or merges `ue_mcp_config.yaml`.
5. Verifies the CLI entrypoint.

The configuration file keeps its historical name:

```yaml
engine_root: D:/UnrealEngine5/UnrealEngine
project_root: D:/UnrealGame/Lyra_56
tcp_port: 55558
daemon_port: 55559
auto_start_daemon: true
```

Use different `tcp_port` and `daemon_port` values for multiple editor
instances.

## Verify CLI Runtime

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\scripts\install_project_launchers.ps1 -ProjectRoot D:\UnrealGame\Lyra_56

cd D:\UnrealGame\Lyra_56
.\ue.ps1 version
.\ue.ps1 doctor
.\ue.ps1 daemon status
.\ue.ps1 query health
.\ue.ps1 run "get_context"
```

The daemon auto-starts for `run`, `query`, and `doctor` unless
`auto_start_daemon: false` is set.

For complex Unreal Python, use a file or stdin instead of shell-quoted code:

```powershell
New-Item -ItemType Directory -Force .\.codex\tmp | Out-Null
@'
import unreal
_result = unreal.SystemLibrary.get_engine_version()
'@ | Set-Content -Encoding UTF8 .\.codex\tmp\check.py
.\ue.ps1 py --json --file .\.codex\tmp\check.py
```

For multi-line CLI DSL batches, use `run --file`:

```powershell
@'
ping
get_context
'@ | Set-Content -Encoding UTF8 .\.codex\tmp\check.uecli
.\ue.ps1 run --json --file .\.codex\tmp\check.uecli
```

## Install The Codex Skill

The plugin ships a reusable skill at `skills/unreal-ue-cli`:

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\scripts\link_codex_skill.ps1
```

After installation, Codex can use `$unreal-ue-cli` or trigger the skill
naturally for Unreal Editor automation tasks. The link keeps the plugin skill
and the Codex skill as one maintained copy.

## Output Modes

Default output is text:

```text
OK get_context
Asset path: /Game/...
Status: ok
```

Use `--json` for scripts and tests:

```powershell
.\ue.ps1 run "get_context" --json
```

Use `--raw` only for low-level debugging:

```powershell
.\ue.ps1 run "get_context" --raw
```

## Troubleshooting

### Daemon Not Running

```powershell
.\ue.ps1 daemon start
.\ue.ps1 daemon status
```

### Unreal Bridge Not Reachable

Check the editor process and port:

```powershell
Get-Process | Where-Object { $_.ProcessName -like "*UnrealEditor*" }
Get-NetTCPConnection -LocalPort 55558 -ErrorAction SilentlyContinue
```

Start the editor with the configured `-MCPPort=<tcp_port>`.

### CLI Parse Error

Ask the CLI for command help:

```powershell
.\ue.ps1 query "help <command>"
.\ue.ps1 query "search <keyword>"
```

### Large Output

Prefer command-specific detail or compact flags first. Use `--json` when exact
fields are required.

## Legacy MCP

For older AI clients, the legacy two-tool MCP server can still be launched:

```powershell
.\Python\.venv\Scripts\python.exe -m ue_cli_tool.server
```

A compatible MCP config uses:

```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "D:/Project/Plugins/UEEditorMCP/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_cli_tool.server"],
      "env": {
        "PYTHONPATH": "D:/Project/Plugins/UEEditorMCP/Python"
      }
    }
  }
}
```

Keep this path for compatibility only. New Codex usage should call `.\ue.ps1`.
