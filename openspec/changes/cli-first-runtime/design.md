# CLI-first Runtime Design

## Current State

Current layers:

```text
Codex MCP tool call
  -> Python ue_cli_tool.server
  -> CliParser / ActionRegistry
  -> PersistentUnrealConnection singleton
  -> C++ FMCPServer on 127.0.0.1:55558
  -> MCPBridge / FEditorAction
```

The durable connection logic already lives in
`Python/ue_cli_tool/connection.py`. The MCP server is only one host for that
connection object.

## Target State

Target layers:

```text
Codex shell command
  -> Python CLI entrypoint
  -> local daemon IPC
  -> daemon-owned PersistentUnrealConnection
  -> C++ FMCPServer on 127.0.0.1:<tcp_port>
  -> MCPBridge / FEditorAction
```

The C++ names may remain `MCP*` internally during migration. The user-facing
runtime should be named around `ue` or `ue-cli`, not MCP.

## Stable Connection Model

The stable connection should live in `ue_cli_tool.daemon`.

Responsibilities:

- Load `ue_mcp_config.yaml`.
- Create one `PersistentUnrealConnection` using configured `tcp_port`.
- Maintain heartbeat, reconnect, circuit breaker, metrics, and context store.
- Accept local requests from CLI processes.
- Serialize requests to Unreal Editor over the existing length-prefixed TCP
  protocol.
- Return normalized internal envelopes and format them for the requested output
  mode.
- Shut down cleanly on explicit `daemon stop` or idle timeout policy.

The CLI process must be stateless by default. It may start the daemon if it is
not running, but it must not own the Unreal connection for normal commands.

## IPC Choice

Use a local TCP loopback daemon first.

Rationale:

- Windows named pipes add platform-specific complexity.
- The project already uses TCP framing and tests for socket behavior.
- A daemon loopback port is easy for Codex to diagnose with PowerShell.

Recommended defaults:

- Unreal editor bridge: configured `tcp_port`, currently `55558`.
- Python daemon IPC: configured `daemon_port`, default `55559`.
- Bind only to `127.0.0.1`.
- Include process id and project root in the daemon lock file.

## CLI Contract

Recommended entrypoint:

```powershell
python Plugins\UEEditorMCP\Python\ue.py <subcommand> [args...]
```

Required subcommands:

- `run <command_text>`: execute CLI command text.
- `python` / `py`: execute Unreal Python code from a positional snippet,
  stdin, or `--file` without passing through the `run` DSL parser.
- `query <query_text>`: help, search, context, logs, metrics, health, skills,
  resources.
- `daemon start`: start daemon in the background.
- `daemon stop`: stop daemon.
- `daemon restart`: stop and start the daemon so Python modules are reloaded.
- `daemon status`: show daemon pid, project root, ports, and UE health.
- `doctor`: run local diagnostics.

`run` must also accept stdin for multi-line scripts.
`python` must also accept stdin and `--file` for PowerShell-safe multi-line
Python execution.

## Output Model

The daemon should keep a structured internal result, but the CLI should return
human-readable text by default.

Output modes:

- `text`: default for Codex and humans.
- `json`: explicit `--json` machine-readable envelope for tests and scripts.
- `raw`: explicit debug mode that returns minimally transformed UE data.

Default text output should be concise, deterministic, and repair-oriented:

```text
OK get_context
Current asset: /Game/Characters/BP_Player
Open editor: BlueprintEditor
Selected: 2 actors

Next: use `ue query help <command>` for command syntax.
```

Errors should also be text-first:

```text
ERROR UE_NOT_CONNECTED
Cannot connect to Unreal Editor on 127.0.0.1:55558.
Recoverable: yes
Next: start Unreal Editor with -MCPPort=55558 or run `ue doctor`.
```

Batch output should be line-oriented:

```text
OK batch 3/3
1 OK create_blueprint BP_Test
2 OK add_blueprint_variable Health
3 OK compile_blueprint
```

When output is too large, the formatter should summarize first and expose a
clear follow-up command for details instead of dumping raw trees.

## Internal Result Envelope

Machine-readable outputs should use this shape when `--json` is requested:

```json
{
  "success": true,
  "kind": "result",
  "data": {},
  "diagnostics": {}
}
```

Failures should use:

```json
{
  "success": false,
  "kind": "error",
  "error": {
    "code": "UE_NOT_CONNECTED",
    "message": "Cannot connect to Unreal Editor on 127.0.0.1:55558",
    "recoverable": true,
    "suggested_next": "Start Unreal Editor with -MCPPort=55558 or run ue doctor"
  },
  "diagnostics": {}
}
```

The daemon may preserve raw C++ result fields internally. Public `text` output
should be stable and compact. Public `json` output should be stable enough for
tests and external scripts.

## Response Formatting Ownership

Response formatting should live in a dedicated Python module, for example
`ue_cli_tool.formatter`.

Responsibilities:

- Convert raw C++ action results into internal result envelopes.
- Convert internal result envelopes into text, JSON, or raw output.
- Apply command-specific summaries for high-volume domains such as Blueprint
  graphs, materials, UMG trees, asset lists, logs, and animation graphs.
- Preserve important identifiers in text output: asset path, object name,
  command name, node id, graph name, counts, warnings, and suggested next
  command.
- Truncate large arrays and nested objects by policy, not by accidental string
  slicing.
- Keep raw details available through explicit `--json`, `--raw`, or follow-up
  detail commands.

## Development Rules

1. Keep model-facing commands textual.
2. Keep JSON internal to Python/UE transport and explicit machine-readable
   output modes.
3. Keep persistent state in the daemon, not the CLI subprocess.
4. Keep C++ action schemas authoritative for command definitions.
5. Add command aliases in Python only when they improve human/AI CLI usage.
6. Return concise repairable text errors with a code and suggested next action.
7. Do not add a new MCP tool for new functionality.
8. Do not require Codex to pass raw JSON unless the command semantically needs
   object data.
9. Use batch execution for multi-line scripts.
10. Add tests at the parser, daemon IPC, connection, formatter, and contract
    levels before removing the MCP server path.

## Migration Strategy

Phase 1 keeps the MCP server working and adds the daemon/CLI path.

Phase 2 updates the Codex skill to prefer shell-based CLI usage and marks MCP
usage as legacy fallback.

Phase 3 removes MCP setup from default installation after the CLI path passes
runtime tests.
