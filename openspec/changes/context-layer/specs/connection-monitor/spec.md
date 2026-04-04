## ADDED Requirements

### Requirement: UE connection state tracking
The system SHALL track the UE TCP connection state as one of: `"alive"`, `"disconnected"`, `"crashed"`, `"reconnecting"`. The state SHALL be persisted in `session.json` and available to the ContextStore.

#### Scenario: Normal connection
- **WHEN** the TCP connection to UE is established and heartbeat is responding
- **THEN** the connection state SHALL be `"alive"`

#### Scenario: Normal disconnection
- **WHEN** the TCP connection is closed gracefully (UE editor closed normally)
- **THEN** the connection state SHALL transition to `"disconnected"`

#### Scenario: Crash detection
- **WHEN** the TCP heartbeat fails or the connection drops unexpectedly (no graceful close)
- **THEN** the connection state SHALL transition to `"crashed"` and the crash timestamp SHALL be recorded in `session.json`

### Requirement: Connection state change callback
The `PersistentUnrealConnection` SHALL support a callback mechanism to notify the ContextStore of connection state changes. The callback SHALL be invoked with `(new_state, old_state, timestamp)` on every state transition.

#### Scenario: Callback on crash
- **WHEN** UE crashes and the connection heartbeat fails
- **THEN** the callback SHALL be invoked with `new_state="crashed"`, and the ContextStore SHALL update `session.json` with `ue_connection: "crashed"` and `crash_time`

#### Scenario: Callback on reconnection
- **WHEN** the connection is re-established after a crash
- **THEN** the callback SHALL be invoked with `new_state="alive"`, and the ContextStore SHALL update `session.json` with `ue_connection: "alive"` and `recovered_from_crash: true`

### Requirement: Crash context preservation
When a crash is detected, the ContextStore SHALL immediately persist the current state (workset, last operation) to disk so that the next `ue_context(action="resume")` call can report what was happening when the crash occurred.

#### Scenario: Resume reports crash context
- **WHEN** UE crashed while `BP_Player` was in the workset with last_op `"graph.connect_nodes"`
- **THEN** `ue_context(action="resume")` SHALL include `crash_context.last_op: "graph.connect_nodes"`, `crash_context.workset` with `BP_Player`, and `crash_context.crash_time`
