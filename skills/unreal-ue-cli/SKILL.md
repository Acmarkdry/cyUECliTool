---
name: unreal-ue-cli
description: Use this skill when working with Unreal Editor projects that have the UEEditorMCP/UECliTool plugin and Codex needs to inspect or automate editor assets through the CLI-first runtime. Use for Blueprint, material, UMG widget, AnimGraph, Niagara, Sequencer, level, asset, log, diagnostics, or editor automation tasks that should run through the PowerShell CLI instead of MCP tool JSON.
---

# Unreal UE CLI

Use the project CLI as the model-facing interface to Unreal Editor. Do not use
MCP tools by default; treat MCP as a legacy fallback only when the user
explicitly asks for it or the CLI path is blocked.

## Command Contract

Prefer the project-root launcher when it exists:

```powershell
.\ue.ps1 query health
.\ue.ps1 py --json --file .\.codex\tmp\task.py
.\ue.ps1 run --json --file .\.codex\tmp\task.uecli
.\ue.ps1 daemon restart
```

If the project-root launcher is missing, use the plugin launcher:

```powershell
.\Plugins\UEEditorMCP\ue.ps1 query health
```

Do not make the model repeat venv paths such as
`Python\.venv\Scripts\python.exe Python\ue.py`. The launchers own that detail.

## Main Workflow

1. Check health before editor work:
   ```powershell
   .\ue.ps1 query health
   .\ue.ps1 query context
   ```

2. For Unreal Python, write a temporary `.py` file and execute it:
   ```powershell
   .\ue.ps1 py --json --file .\.codex\tmp\task.py
   ```
   Set `_result = ...` for structured data. Use `print()` only for log text.
   Do not print the same object assigned to `_result`; stdout and return value
   are separate output channels. Text output hides obvious duplicates, but JSON
   output intentionally preserves both fields for diagnostics.

3. For CLI DSL batches, write a temporary `.uecli` file and execute it:
   ```powershell
   .\ue.ps1 run --json --file .\.codex\tmp\task.uecli
   ```
   Use `@Target` to fill context parameters such as `blueprint_name`,
   `material_name`, or `widget_name`.
   `run --file` stops on the first failed command by default. Use
   `--continue-on-error` only for independent read-only batches.

4. Use query commands for discovery and recovery:
   ```powershell
   .\ue.ps1 query help
   .\ue.ps1 query "help get_blueprint_summary"
   .\ue.ps1 query "search material"
   .\ue.ps1 query "logs --n 50 --source editor"
   ```

## Temporary Files

Use `.codex\tmp` under the project root for generated scripts. Create it when
missing:

```powershell
New-Item -ItemType Directory -Force .\.codex\tmp | Out-Null
```

Keep generated scripts focused on one task. Prefer Python files for complex
logic, loops, object construction, or anything involving quotes/newlines.
Prefer `.uecli` files for short batches of built-in commands.

## Startup And Recovery

- `DAEMON_NOT_RUNNING`: run `.\ue.ps1 daemon start`.
- Daemon loaded old Python modules: run `.\ue.ps1 daemon restart`, then
  `.\ue.ps1 daemon status --json` and inspect `data.source`.
- Unreal Editor not reachable: start the project editor with `-MCPPort=<tcp_port>`
  from `ue_mcp_config.yaml`, then run `.\ue.ps1 query health`. `query health`
  and `doctor` return non-zero when the editor is disconnected.
- Parse errors: run `.\ue.ps1 query "help <command>"` or switch the work to
  `.\ue.ps1 py --file`.
- Large output: use command-specific detail flags or `--json`; avoid `--raw`
  unless debugging transport payloads.

## Safety Rules

- Read before write: inspect context/assets first.
- Keep batches small enough that each child result is reviewable.
- Prefer `_result` for exact data and assertions.
- Rely on `_result` for Unreal arrays and object summaries; the CLI converts
  common Unreal Python values into JSON-compatible structures.
- Do not use PowerShell here-strings as the main path for complex Python; write
  a `.py` file and call `py --file`.
- Do not call MCP tools (`ue_cli`, `ue_query`) unless the CLI path is blocked.
