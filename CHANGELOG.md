# Changelog

## 0.6.5

- Add `describe_asset` CLI alias with prefix-based routing (AB_/AM_/BS_/SK_ → specialized describe commands).
- Register `describe_asset` in the action registry so positional args map correctly.
- Unknown commands now return `Did you mean:` suggestions from the registry search.
- Alias resolution applies in daemon `_send_command`, `handle_cli`, and batch runs.

## 0.6.4

- Add `run --params '<json>'` to pass command parameters without fragile shell quoting.

## 0.6.3

- Clear stale `last_error` from `query health` after a successful UE command.

## 0.6.2

- Add `describe_linked_anim_stack` to traverse linked AnimBP layers (linked inputs,
  child blueprint paths, state machines, depth/parent metadata).

## 0.6.1

- Export AnimGraph `property_bindings[]` from `get_state_subgraph` and
  `describe_animgraph_topology` (pin, path, friendly_name, property_path).
- Bound anim pins now surface as `is_bound` / `is_connected` even without wire links.
- `get_transition_rule` parses `K2Node_PropertyAccess` into `property_path` and
  `referenced_property_paths`.
- `describe_anim_blueprint_full` recursively lists nested state machines with
  `parent_state_machine`, `parent_state`, and `depth`.

## 0.6.0

- Make the PowerShell launcher (`ue.ps1`) and Codex skill the primary workflow.
- Add `ue version` and `ue --version`.
- Improve text output for `doctor`, `daemon status`, `query health`, and
  `exec_python`.
- Keep `stdout` and `_result` as separate channels; text output now hides empty
  streams and shows one clear `Return value`.
- Remove duplicate `_cli_line` from JSON `data`; the canonical copy is the
  envelope-level `cli_line`.
- Support PowerShell pipeline input in project and plugin launchers.
- Execute multi-line `run --file` scripts sequentially through the daemon.
- Stop `run --file` batches at the first failed command by default; add
  `--continue-on-error` for explicit best-effort batches.
- Return failing envelopes and non-zero CLI exit codes for disconnected
  `query health` and `doctor` checks.
- Increase daemon client timeouts for `run` and `py` editor work so long UE
  actions are not cut off at 30 seconds.
- Convert common Unreal Python `_result` values, including Unreal arrays and
  objects, into JSON-compatible CLI return values.
- Serialize daemon access to the shared Unreal TCP connection.
- Register the C++ `batch_execute` action with `MCPBridge`.

## 0.5.0

- Move the model-facing interface from MCP tool JSON to CLI-first commands.
- Add the Python daemon, text/json/raw formatter, and reusable Codex skill.
- Preserve the legacy MCP server for compatibility.
