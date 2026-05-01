# CLI-first Runtime

## Summary

Replace the Codex-visible MCP interface with a local CLI-first runtime.
Codex should emit CLI text, a Python entrypoint should parse and normalize it,
and a long-lived local daemon should own the persistent Unreal Editor
connection.

## Motivation

The current design already exposes only two MCP tools, but it still forces
Codex through MCP JSON tool calls. That keeps the weakest part of the system in
the model-facing boundary: JSON schema construction, MCP argument validation,
and nested JSON result handling.

The desired boundary is simpler:

```text
Codex -> shell command -> CLI text -> Python parser -> daemon -> UE TCP bridge
```

This preserves the existing C++ editor action investment while removing MCP as
the model-facing protocol.

## Goals

- Make CLI text the only model-authored command format.
- Keep one stable persistent connection to Unreal Editor outside short-lived
  CLI subprocesses.
- Preserve the existing C++ `FMCPServer`, `MCPBridge`, action handlers, and
  length-prefixed TCP protocol during migration.
- Keep command discovery, help, context, metrics, and logs available through
  CLI/query commands.
- Make failures easy for Codex to repair by returning compact, normalized,
  structured errors.
- Return human-readable text to Codex by default; keep JSON behind explicit
  machine-readable flags.
- Create a Codex skill that teaches direct CLI usage instead of MCP tool usage.

## Non-goals

- Do not rewrite all C++ editor actions.
- Do not remove the editor TCP bridge in the first migration.
- Do not require Codex to author JSON request objects for normal commands.
- Do not keep MCP as the primary path after the new CLI runtime is stable.

## Architecture Decision

Persistent connection belongs in a daemon process, not in each CLI invocation.

Short-lived commands such as:

```powershell
ue "get_context"
ue "@BP_Player`ncompile_blueprint"
```

should connect to the local daemon over a lightweight local IPC channel. The
daemon owns `PersistentUnrealConnection`, heartbeats, reconnects, circuit
breaker state, metrics, and context store.

## User Impact

Codex instances will stop calling `ue_cli(...)` and `ue_query(...)` MCP tools.
Instead, the skill will instruct them to run shell commands such as:

```powershell
python D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP\Python\ue.py query health
python D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP\Python\ue.py run "get_context"
```

The default output is concise text optimized for model reading. JSON remains
available for tests, scripts, and debugging through an explicit flag.
