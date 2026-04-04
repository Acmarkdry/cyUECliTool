## ADDED Requirements

### Requirement: Session lifecycle management
The ContextStore SHALL manage session lifecycle with unique session IDs. On startup, the system SHALL load the previous session state from `.context/session.json`. If the previous session status is not `"ended"`, it SHALL be marked as `"previous_session_abnormal"`. A new session with a fresh UUID SHALL be created on each startup. On graceful shutdown, the session status SHALL be set to `"ended"`.

#### Scenario: Normal startup with no prior session
- **WHEN** the MCP server starts and `.context/session.json` does not exist
- **THEN** a new session SHALL be created with a UUID, status `"active"`, and start timestamp

#### Scenario: Startup after normal shutdown
- **WHEN** the MCP server starts and the previous session has status `"ended"`
- **THEN** the previous session SHALL be archived and a new session created
- **THEN** `ue_context(action="resume")` SHALL report `session_status: "previous_session_ended_normally"`

#### Scenario: Startup after abnormal termination
- **WHEN** the MCP server starts and the previous session has status `"active"` (process was killed)
- **THEN** the previous session SHALL be marked `"previous_session_abnormal"`
- **THEN** `ue_context(action="resume")` SHALL report `session_status: "previous_session_abnormal"`

#### Scenario: Graceful shutdown
- **WHEN** the MCP server process exits normally
- **THEN** the session status SHALL be updated to `"ended"` with an end timestamp in `session.json`

### Requirement: Operation history recording
The ContextStore SHALL automatically record every tool invocation as a history entry in `history.jsonl`. Each entry SHALL contain `{timestamp, tool, action_id, params_summary, success, result_summary, duration_ms}`. The `params_summary` SHALL extract key fields (asset names, code truncated to 80 chars) rather than storing full params. History SHALL be capped at 500 entries; older entries SHALL be truncated on startup.

#### Scenario: Successful operation recorded
- **WHEN** `_handle_tool` completes a `ue_actions_run` call with `action_id="graph.describe"` successfully
- **THEN** a history entry SHALL be appended with `tool: "ue_actions_run"`, `action_id: "graph.describe"`, `success: true`, and a `result_summary`

#### Scenario: Failed operation recorded
- **WHEN** `_handle_tool` completes a call that returns an error
- **THEN** a history entry SHALL be appended with `success: false` and the error message in `result_summary`

#### Scenario: Python exec code truncation
- **WHEN** `ue_python_exec` is called with a `code` param of 500 characters
- **THEN** the `params_summary` SHALL contain the code truncated to 80 characters with `"..."` suffix

#### Scenario: History cap enforcement
- **WHEN** the MCP server starts and `history.jsonl` contains 600 entries
- **THEN** the oldest 100 entries SHALL be removed, leaving 500

### Requirement: Working set tracking
The ContextStore SHALL automatically extract asset paths from tool invocation parameters and maintain a working set in `workset.json`. Known asset parameter keys include `blueprint_name`, `material_name`, `asset_path`, `widget_name`, `anim_blueprint`, `mapping_context`. Each workset entry SHALL contain `{path, first_seen, last_op, last_op_time, op_count}`.

#### Scenario: Asset auto-detected from params
- **WHEN** `ue_actions_run` is called with `params.blueprint_name = "BP_Player"`
- **THEN** `BP_Player` SHALL be added to the workset (or updated if already present) with `last_op` set to the action_id

#### Scenario: Multiple assets in batch
- **WHEN** `ue_batch` is called with 3 actions referencing `BP_Player` and `BP_Enemy`
- **THEN** both `BP_Player` and `BP_Enemy` SHALL be in the workset with updated `op_count`

#### Scenario: Stale workset cleanup
- **WHEN** `ue_context(action="resume")` is called and a workset entry has `last_op_time` older than 7 days
- **THEN** that entry SHALL be automatically removed from the workset

### Requirement: File persistence and recovery
The ContextStore SHALL persist all state to `.context/` directory using JSON files. All file reads SHALL be wrapped in try/except; corrupted files SHALL be replaced with empty defaults and a warning logged. File writes SHALL use atomic write (write to temp + rename) to prevent corruption.

#### Scenario: Corrupted session.json
- **WHEN** the MCP server starts and `session.json` contains invalid JSON
- **THEN** the file SHALL be replaced with a fresh session state and a warning logged

#### Scenario: Missing .context directory
- **WHEN** the MCP server starts and `.context/` does not exist
- **THEN** the directory SHALL be created automatically

#### Scenario: Atomic write safety
- **WHEN** a file write is interrupted (e.g., process killed during write)
- **THEN** the previous valid file SHALL remain intact (temp file may be left behind)
