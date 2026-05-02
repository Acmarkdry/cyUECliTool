# Runtime Specification

## ADDED Requirements

### Requirement: CLI-first model boundary

The system SHALL expose Unreal Editor automation to Codex through shell-invoked
CLI text commands as the primary interface.

#### Scenario: Execute a single command

- **WHEN** Codex runs `ue run "get_context"`
- **THEN** the CLI SHALL parse `get_context` using the existing command
  registry
- **AND** return a concise text result by default.

#### Scenario: Execute a multi-line script

- **WHEN** Codex pipes a multi-line script to `ue run`
- **THEN** the CLI SHALL parse the script with context target support
- **AND** execute it through batch execution.

#### Scenario: Execute Unreal Python through the dedicated path

- **WHEN** Codex runs `ue python --file script.py` or pipes stdin to
  `ue python`
- **THEN** the CLI SHALL send an `exec_python` daemon request directly
- **AND** SHALL NOT split the Python source through the `run` DSL parser.

### Requirement: Daemon-owned persistent Unreal connection

The system SHALL keep the persistent Unreal Editor connection in a long-lived
local daemon process.

#### Scenario: Multiple CLI invocations

- **WHEN** Codex invokes `ue run` multiple times
- **THEN** each CLI subprocess SHALL communicate with the daemon
- **AND** the daemon SHALL reuse its existing Unreal Editor connection when
  healthy.

#### Scenario: Unreal connection drops

- **WHEN** the daemon detects a broken Unreal Editor connection
- **THEN** it SHALL apply the existing reconnect and circuit breaker behavior
- **AND** report recoverable health information to CLI callers.

### Requirement: Local-only transport

The daemon and Unreal Editor bridge SHALL bind only to loopback addresses by
default.

#### Scenario: Daemon startup

- **WHEN** the daemon starts
- **THEN** it SHALL bind to `127.0.0.1:<daemon_port>`
- **AND** write status information that includes pid, project root,
  `daemon_port`, and `tcp_port`.

#### Scenario: Daemon module reload

- **WHEN** Codex runs `ue daemon restart`
- **THEN** the CLI SHALL stop any running daemon before starting a new one
- **AND** `daemon status` SHALL expose source module paths and mtimes so callers
  can verify the active daemon loaded the expected Python files.

### Requirement: Text-first model output

The CLI SHALL return concise human-readable text by default.

#### Scenario: Successful read command

- **WHEN** Codex runs a read command without output flags
- **THEN** the CLI SHALL return deterministic text with the command status,
  key fields, counts, warnings, and useful next command when applicable
- **AND** SHALL NOT dump raw JSON by default.

#### Scenario: Successful batch command

- **WHEN** Codex runs multiple commands as a batch
- **THEN** the CLI SHALL return a line-oriented text summary
- **AND** include one status line per child command.

#### Scenario: Large result

- **WHEN** a command returns a large nested result
- **THEN** the CLI SHALL summarize the result first
- **AND** provide a follow-up command or flag for details.

### Requirement: Explicit machine-readable output

The CLI SHALL provide JSON only when explicitly requested.

#### Scenario: JSON output requested

- **WHEN** Codex or a script passes `--json`
- **THEN** the CLI SHALL return the stable success/error envelope.

#### Scenario: Raw output requested

- **WHEN** Codex or a script passes `--raw`
- **THEN** the CLI SHALL return minimally transformed daemon or UE data for
  debugging.

### Requirement: Normalized internal envelope

The daemon SHALL normalize raw UE results into stable internal success and
error envelopes before formatting them.

#### Scenario: Parse failure

- **WHEN** CLI text cannot be parsed
- **THEN** the internal envelope SHALL include `success: false`
- **AND** an error object with code, message, recoverable flag, and suggested
  next action.

#### Scenario: UE transport failure

- **WHEN** the daemon cannot reach Unreal Editor
- **THEN** the internal envelope SHALL include a stable transport error code
- **AND** enough diagnostics to run `ue doctor`.

### Requirement: Dedicated response formatter

The system SHALL centralize public output formatting in Python.

#### Scenario: Command returns raw C++ fields

- **WHEN** the daemon receives a raw C++ action result
- **THEN** a Python formatter SHALL decide which fields appear in default text
  output
- **AND** keep the full details available through explicit machine-readable or
  raw modes.

### Requirement: MCP path is legacy fallback

The MCP server SHALL remain available only as a temporary compatibility path
during migration.

#### Scenario: Skill guidance

- **WHEN** Codex loads the new UE CLI skill
- **THEN** the skill SHALL instruct shell-based CLI usage first
- **AND** mention MCP only as legacy fallback.
