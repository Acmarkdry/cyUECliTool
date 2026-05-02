# cyUECliTool

[English](README.md) | [中文](README.zh-CN.md)

CLI-first AI tool for controlling Unreal Engine Editor.

v0.6.0 makes the PowerShell launcher and Codex skill the primary interface.
Codex and other agents should call the local `.\ue.ps1` launcher, write complex
Python or command batches to files, and use the daemon-backed CLI instead of
MCP tool JSON. A Python daemon owns the persistent Unreal Editor connection,
and default output is concise text optimized for model reading.

MCP support remains as a legacy compatibility path during migration.

## Quick Start

```powershell
# 1. Compile the UE project so the editor plugin is available.

# 2. Install/setup the Python environment.
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\setup_mcp.ps1

# 3. Start Unreal Editor. The C++ bridge listens on tcp_port, default 55558.
D:\UnrealEngine5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe `
  D:\UnrealGame\Lyra_56\Lyra_56.uproject -MCPPort=55558

# 4. Install project-root launchers and use the CLI-first runtime.
.\scripts\install_project_launchers.ps1 -ProjectRoot D:\UnrealGame\Lyra_56
cd D:\UnrealGame\Lyra_56
.\ue.ps1 version
.\ue.ps1 query health
.\ue.ps1 run "get_context"
```

The daemon auto-starts by default for `run`, `query`, `py`, and `doctor`
commands.

## Codex Skill

The plugin ships a reusable Codex skill at `skills/unreal-ue-cli`. Link it into
the Codex skill directory so the plugin copy remains the single source of
truth:

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\scripts\link_codex_skill.ps1
```

After installation, agents can invoke `$unreal-ue-cli` or trigger it naturally
when working on Unreal Editor automation tasks.

## Architecture

```text
Codex / user
  -> ue.ps1 run/query/py/doctor
  -> local Python daemon on 127.0.0.1:55559
  -> PersistentUnrealConnection
  -> Unreal Editor C++ bridge on 127.0.0.1:55558
  -> MCPBridge / FEditorAction handlers
```

The C++ bridge and action classes are preserved. The key change is the
model-facing boundary: agents write CLI text, not MCP JSON.

## CLI Usage

Run a single command:

```powershell
.\ue.ps1 run "create_blueprint BP_Player --parent_class Character"
```

Run multiple commands with context through a PowerShell-safe file:

```powershell
New-Item -ItemType Directory -Force .\.codex\tmp | Out-Null
@'
@BP_Player
add_component_to_blueprint CapsuleComponent Capsule
add_blueprint_variable Health --variable_type Float
compile_blueprint
'@ | Set-Content -Encoding UTF8 .\.codex\tmp\task.uecli
.\ue.ps1 run --file .\.codex\tmp\task.uecli
```

Shortcut form:

```powershell
.\ue.ps1 "get_context"
```

Query help and diagnostics:

```powershell
.\ue.ps1 version
.\ue.ps1 query help
.\ue.ps1 query "help create_blueprint"
.\ue.ps1 query "search material"
.\ue.ps1 query "logs --n 50 --source editor"
.\ue.ps1 doctor
```

Execute Unreal Python directly, bypassing the run-DSL parser:

```powershell
@'
import unreal
_result = unreal.SystemLibrary.get_engine_version()
'@ | Set-Content -Encoding UTF8 .\.codex\tmp\task.py
.\ue.ps1 py --json --file .\.codex\tmp\task.py
```

Use `_result` for structured return data. Use `print()` only for logs; printing
the same object assigned to `_result` will show both `Stdout` and
`Return value` because they are separate channels.

## Output Modes

Default output is text:

```text
OK get_context
Asset path: /Game/Characters/BP_Player
Status: ok
```

Use JSON only when a script or test needs a stable machine-readable envelope:

```powershell
.\ue.ps1 run "get_context" --json
```

Use raw mode for low-level debugging:

```powershell
.\ue.ps1 run "get_context" --raw
```

## Daemon Commands

```powershell
.\ue.ps1 daemon start
.\ue.ps1 daemon status
.\ue.ps1 daemon stop
.\ue.ps1 daemon serve
```

The daemon owns:

- Persistent Unreal TCP connection.
- Heartbeat and reconnect behavior.
- Circuit breaker state.
- Metrics and operation context.
- Text/JSON/raw result envelopes for CLI callers.

## Project Configuration

`ue_mcp_config.yaml` is loaded from the project tree:

```yaml
engine_root: D:/UnrealEngine5/UnrealEngine
project_root: D:/UnrealGame/Lyra_56
tcp_port: 55558
daemon_port: 55559
auto_start_daemon: true
```

Use different `tcp_port` and `daemon_port` values when running multiple editor
instances.

## CLI Syntax

```text
<command> [positional_args...] [--flag value ...]
@<target>     Set context for blueprint/material/widget commands
# comment     Ignored
```

Positional arguments are mapped from the command schema. The `@target` context
fills the first matching context parameter such as `blueprint_name`,
`material_name`, or `widget_name`.

Array and object shorthand:

```text
--items a,b,c
--values 1,2,3
--props name=Sword,damage=50
```

JSON values still work when object data is truly needed.

## Legacy MCP Path

The legacy MCP server is still available:

```powershell
.\Python\.venv\Scripts\python.exe -m ue_cli_tool.server
```

It exposes the old two-tool interface, `ue_cli` and `ue_query`, for clients that
have not migrated yet. New development should target the CLI-first path.

## Development

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\Python\.venv\Scripts\python.exe -m pytest Python\tests -q
.\Python\.venv\Scripts\python.exe -m pytest tests -q
```

Key Python modules:

| Module | Purpose |
|--------|---------|
| `ue_cli_tool.cli` | Short-lived CLI entrypoint |
| `ue_cli_tool.daemon` | Long-lived local daemon |
| `ue_cli_tool.runtime` | MCP-free command/query runtime |
| `ue_cli_tool.formatter` | Text/json/raw output formatting |
| `ue_cli_tool.connection` | Persistent Unreal TCP connection |
| `ue_cli_tool.cli_parser` | CLI syntax parser |

## Documentation

| Document | Description |
|----------|-------------|
| [Installation](docs/installation.md) | CLI-first setup and troubleshooting |
| [Architecture](docs/architecture.md) | Technical details, C++ server, event system, protocols |
| [Actions](docs/actions.md) | Action domain reference |
| [Development](docs/development.md) | Adding new actions, tests, commandlet mode |
| [CLI-first Migration](docs/cli-first-migration.md) | Migration status and remaining compatibility notes |
| [GitHub Actions Runner](docs/github-actions-runner.md) | Self-hosted Windows runner setup |
| [Changelog](CHANGELOG.md) | Release notes |

## Credits

Maintained by Acmarkdry with Codex-assisted development.

Based on [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP)
(MIT License).

## License

MIT
