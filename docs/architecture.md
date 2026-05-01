# Architecture

## v0.5.0 CLI-first Runtime

The primary model-facing interface is now a shell-invoked CLI. Agents write
plain command text; they do not author MCP tool JSON for normal workflows.

```text
Codex / user
  -> Python/ue.py run/query/doctor
  -> ue_cli_tool.cli
  -> local daemon IPC on 127.0.0.1:<daemon_port>
  -> ue_cli_tool.daemon
  -> PersistentUnrealConnection
  -> C++ FMCPServer on 127.0.0.1:<tcp_port>
  -> MCPBridge
  -> FEditorAction handlers
```

The C++ bridge still uses the historical `MCP*` names internally. The public
runtime is CLI-first.

## Ports

| Port | Owner | Default | Purpose |
|------|-------|---------|---------|
| `tcp_port` | Unreal Editor plugin | `55558` | Python daemon to Unreal Editor bridge |
| `daemon_port` | Python daemon | `55559` | Short-lived CLI process to daemon IPC |

Both ports bind to `127.0.0.1` by default.

## Python Layers

| Module | Responsibility |
|--------|----------------|
| `ue_cli_tool.cli` | Short-lived CLI entrypoint and daemon lifecycle commands |
| `ue_cli_tool.daemon` | Long-lived process that owns the UE connection |
| `ue_cli_tool.runtime` | MCP-free command/query handlers |
| `ue_cli_tool.formatter` | Text/json/raw output formatting |
| `ue_cli_tool.connection` | Persistent TCP connection, heartbeat, reconnect, circuit breaker |
| `ue_cli_tool.cli_parser` | CLI text parser and schema-derived positional mapping |
| `ue_cli_tool.registry` | Python command registry derived from action definitions |
| `ue_cli_tool.context` | Session, operation history, and working-set persistence |

## Stable Connection Model

`ue_cli_tool.daemon` owns one `PersistentUnrealConnection` per project/port
pair. CLI invocations are stateless:

```text
ue run "get_context"
  -> connect to daemon
  -> send request
  -> print formatted response
  -> exit
```

This avoids reconnecting to Unreal Editor for every command while keeping the
model-facing command path simple.

## Output Model

Default output is concise text:

```text
OK get_context
Asset path: /Game/Characters/BP_Player
Status: ok
```

Machine-readable JSON is explicit:

```powershell
python .\Python\ue.py run "get_context" --json
```

Raw debugging output is explicit:

```powershell
python .\Python\ue.py run "get_context" --raw
```

## Command Flow

Single command:

```text
CLI text
  -> CliParser
  -> CommandDict(command, params)
  -> daemon send_command
  -> PersistentUnrealConnection
  -> length-prefixed JSON over TCP
  -> C++ action execution on Game Thread
  -> raw result
  -> internal envelope
  -> text/json/raw formatting
```

Multi-line command scripts are converted into `batch_execute` and sent as one
Unreal bridge request.

## C++ Bridge

`FMCPServer` accepts length-prefixed JSON messages from Python:

```json
{"type": "get_context", "params": {}}
```

It handles fast-path commands such as `ping`, `close`, async task polling, and
event polling directly. Editor mutations and queries are dispatched to the Game
Thread and executed through `UMCPBridge`.

`UMCPBridge` maps command names to `FEditorAction` instances. Each action:

- validates parameters,
- optionally opens an undo transaction,
- executes on editor objects,
- returns a `FJsonObject` with `success` plus action-specific fields.

## Response Ownership

Return data has three layers:

1. C++ actions produce raw action fields.
2. `connection.py` normalizes transport-level success/failure compatibility.
3. `formatter.py` decides what text Codex sees by default.

Large arrays and nested objects should be summarized in text output and kept
available through `--json`, `--raw`, or follow-up detail commands.

## Legacy MCP Path

`ue_cli_tool.server` still exposes the legacy two-tool MCP interface:

- `ue_cli`
- `ue_query`

It remains available for compatibility. New work should target the CLI-first
runtime.
