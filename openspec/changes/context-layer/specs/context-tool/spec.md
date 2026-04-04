## ADDED Requirements

### Requirement: ue_context tool registration
The system SHALL register a new MCP tool named `ue_context` in the TOOLS list of `server_unified.py`. The tool SHALL accept an `action` parameter (required, enum: `"resume"`, `"status"`, `"history"`, `"workset"`, `"clear"`) and an optional `limit` parameter (integer, default 20, for history action).

#### Scenario: Tool listed in capabilities
- **WHEN** an MCP client calls `tools/list`
- **THEN** `ue_context` SHALL appear in the tool list with description and input schema

### Requirement: Resume action
The `ue_context(action="resume")` SHALL return a comprehensive context recovery payload containing: previous session summary (status, duration, op count), current UE connection state, current working set (asset paths with last operation), and the most recent 10 operation history entries. This is the primary tool for AI to recover context after starting a new conversation.

#### Scenario: Resume after normal session
- **WHEN** AI calls `ue_context(action="resume")` and the previous session ended normally
- **THEN** the response SHALL contain `previous_session.status: "ended_normally"`, `previous_session.duration`, `previous_session.op_count`, `ue_connection: "alive"|"disconnected"`, `workset: [...]`, and `recent_ops: [...]`

#### Scenario: Resume after UE crash
- **WHEN** AI calls `ue_context(action="resume")` and UE crashed during the previous session
- **THEN** the response SHALL contain `previous_session.status: "ue_crashed"`, `previous_session.last_known_ue_state: "crashed"`, and `recovery_hint: "UE may have auto-saved. Reconnect and verify working_set assets."`

#### Scenario: Resume with no previous session
- **WHEN** AI calls `ue_context(action="resume")` and no previous session exists
- **THEN** the response SHALL contain `previous_session: null`, `ue_connection` current state, and empty workset/recent_ops

### Requirement: Status action
The `ue_context(action="status")` SHALL return the current session's real-time status: session ID, session start time, UE connection state, total operation count, working set size, and last operation timestamp.

#### Scenario: Status check
- **WHEN** AI calls `ue_context(action="status")` mid-session
- **THEN** the response SHALL contain `session_id`, `started_at`, `ue_connection`, `op_count`, `workset_size`, `last_op_at`

### Requirement: History action
The `ue_context(action="history", limit=N)` SHALL return the most recent N operation history entries (default 20, max 100). Each entry SHALL contain `timestamp`, `tool`, `action_id`, `params_summary`, `success`, `result_summary`, `duration_ms`.

#### Scenario: History with default limit
- **WHEN** AI calls `ue_context(action="history")` without specifying limit
- **THEN** the response SHALL contain the most recent 20 history entries

#### Scenario: History with custom limit
- **WHEN** AI calls `ue_context(action="history", limit=5)`
- **THEN** the response SHALL contain the most recent 5 history entries

### Requirement: Workset action
The `ue_context(action="workset")` SHALL return the complete working set with all tracked assets and their metadata (path, first_seen, last_op, last_op_time, op_count).

#### Scenario: Workset query
- **WHEN** AI calls `ue_context(action="workset")` after operating on BP_Player and M_Character
- **THEN** the response SHALL contain entries for both assets with correct operation counts

### Requirement: Clear action
The `ue_context(action="clear")` SHALL reset the working set and operation history, effectively starting a clean session while preserving the session ID.

#### Scenario: Clear context
- **WHEN** AI calls `ue_context(action="clear")`
- **THEN** the workset SHALL be empty, the history SHALL be empty, and subsequent `ue_context(action="status")` SHALL show `op_count: 0`
