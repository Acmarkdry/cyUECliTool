# CLI-first Migration Notes

This document tracks the v0.5.0 through v0.6.0 move from model-facing MCP tools
to the CLI-first runtime.

## Completed

- Added `Python/ue.py` as the primary entrypoint.
- Added `ue_cli_tool.cli` for short-lived commands.
- Added `ue_cli_tool.daemon` to own the persistent Unreal connection.
- Added `ue_cli_tool.formatter` for text/json/raw output modes.
- Added `ue_cli_tool.runtime` so command/query handling no longer depends on
  the MCP server module.
- Added reusable Codex skill at `skills/unreal-ue-cli`.
- Updated README and core docs to CLI-first.
- Changed setup scripts to validate the CLI entrypoint instead of generating
  MCP client config by default.
- Removed obsolete `.kiro` planning/spec files.
- Added project-root PowerShell launchers so agents can call `.\ue.ps1`.
- Added `ue py` / `ue python` direct execution for PowerShell-safe Unreal
  Python via `--file` or stdin.
- Added `ue run --file` for multi-line CLI DSL scripts.
- Made daemon requests serialize access to the shared Unreal TCP connection.
- Improved text diagnostics for `doctor`, `daemon status`, `query health`, and
  `exec_python`.

## Preserved Compatibility

- `ue_cli_tool.server` still exposes the legacy MCP tools for older clients.
- C++ classes keep historical names such as `FMCPServer`, `MCPBridge`, and
  `UEEditorMCPCommandlet`.
- Config keeps the historical filename `ue_mcp_config.yaml`.
- Editor command-line override remains `-MCPPort=<tcp_port>`.

These names are compatibility details, not the public model-facing path.

## Follow-up Candidates

- Rename public setup script aliases in a future release, for example add
  `setup_cli.ps1` while keeping `setup_mcp.ps1` as a shim.
- Split `mcp` into an optional Python extra once legacy server usage is no
  longer common.
- Add more command-specific text formatters for very large result types.
- Add a packaged skill installation command to setup scripts if Codex skill
  discovery standardizes further.
- Consider renaming C++ classes only if the compatibility cost becomes small.

## Current Recommendation

Use the project launcher form:

```powershell
.\ue.ps1 doctor
.\ue.ps1 query help
.\ue.ps1 py --json --file .\.codex\tmp\task.py
.\ue.ps1 run --json --file .\.codex\tmp\task.uecli
```

Do not use `ue_cli(...)` or `ue_query(...)` unless testing legacy MCP
compatibility.
