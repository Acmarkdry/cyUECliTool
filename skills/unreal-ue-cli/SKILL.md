---
name: unreal-ue-cli
description: Use this skill when working with Unreal Editor projects that have the UEEditorMCP/UECliTool plugin and Codex needs to inspect or automate editor assets through the CLI-first runtime. Use for Blueprint, material, UMG widget, AnimGraph, Niagara, Sequencer, level, asset, log, diagnostics, or editor automation tasks that should run through Python/ue.py instead of MCP tool JSON.
---

# Unreal UE CLI

Use the project's `Python/ue.py` CLI as the model-facing interface to Unreal
Editor. Treat MCP as a legacy fallback only when the user explicitly asks for it
or the CLI path is unavailable.

## Locate The CLI

Prefer the plugin-local Python venv when present:

```powershell
$Plugin = "D:\Path\To\Project\Plugins\UEEditorMCP"
$Py = Join-Path $Plugin "Python\.venv\Scripts\python.exe"
$Ue = Join-Path $Plugin "Python\ue.py"
& $Py $Ue query health
```

If the plugin path is not known, search from the project root:

```powershell
Get-ChildItem -Recurse -Filter ue.py | Where-Object { $_.FullName -like "*UEEditorMCP*Python*" }
```

Fallback to `python <plugin>\Python\ue.py ...` only when the venv is missing.

## Health Workflow

Start with cheap diagnostics before editor mutations:

```powershell
& $Py $Ue doctor
& $Py $Ue daemon status
& $Py $Ue query health
& $Py $Ue query context
```

If the daemon is not running, start it:

```powershell
& $Py $Ue daemon start
```

If Unreal Editor is not reachable, inspect project config and editor process:

```powershell
Get-Content .\ue_mcp_config.yaml
Get-Process | Where-Object { $_.ProcessName -like "*UnrealEditor*" }
Get-NetTCPConnection -LocalPort 55558 -ErrorAction SilentlyContinue
```

Start Unreal Editor with the configured `tcp_port` when needed. If
`ue_mcp_config.yaml` contains `engine_root` and `project_root`, derive the
editor path and `.uproject` from those values.

## Command Discovery

Do not guess command syntax when help is available:

```powershell
& $Py $Ue query help
& $Py $Ue query "help create_blueprint"
& $Py $Ue query "search material"
& $Py $Ue query "skills"
```

The default output is text written for Codex/humans. Use `--json` for tests,
scripts, or precise field extraction. Use `--raw` only for debugging low-level
daemon or UE bridge payloads.

## Running Commands

Single command:

```powershell
& $Py $Ue run "get_context"
& $Py $Ue run "get_blueprint_summary BP_Player --detail_level normal"
```

Shortcut form is allowed:

```powershell
& $Py $Ue "get_context"
```

Multi-command batch with context:

```powershell
@"
@BP_Player
add_component_to_blueprint CapsuleComponent Capsule
add_blueprint_variable Health --variable_type Float
compile_blueprint
"@ | & $Py $Ue run
```

`@Target` fills the first matching context parameter such as
`blueprint_name`, `material_name`, or `widget_name`.

## Output Handling

Read default text output first. It should contain status, key fields, counts,
warnings, and suggested next actions.

Use JSON only when the task needs machine-readable assertions:

```powershell
& $Py $Ue run "get_context" --json
```

When output says a result was summarized, request a more specific command or add
the command's own detail flag, such as `--detail_level full`, before using
`--raw`.

## Safety Rules

- Use read-only commands first to identify the target asset.
- Use `query "help <command>"` before unfamiliar write commands.
- Keep batches focused and review each child result line.
- Prefer `@Target` context for Blueprint/material/widget workflows.
- Avoid `--raw` as default; it can produce large nested payloads.
- Do not call MCP tools (`ue_cli`, `ue_query`) unless the CLI path is blocked.

## Common Recovery

- `DAEMON_NOT_RUNNING`: run `daemon start`, then retry.
- `UE_NOT_CONNECTED` or transport errors: start Unreal Editor with the configured `-MCPPort`, then run `doctor`.
- Parse errors: run `query "help <command>"` and fix the CLI line.
- Large output: use command-specific compact/detail flags or `--json` for exact fields.
