# Tasks

## 1. Runtime Boundary

- [ ] Add `ue_cli_tool.daemon` that owns `PersistentUnrealConnection`.
- [ ] Add local daemon IPC on `127.0.0.1:<daemon_port>`.
- [ ] Add daemon lock/status file under `.context`.
- [ ] Add daemon lifecycle commands: `start`, `stop`, `status`.
- [ ] Add idle/stale daemon detection and cleanup.

## 2. CLI Entrypoint

- [ ] Add `Python/ue.py` or equivalent console script.
- [ ] Implement `run <command_text>` with stdin support.
- [ ] Implement `query <query_text>`.
- [ ] Implement `doctor`.
- [ ] Auto-start daemon on first command when configured.
- [ ] Preserve direct one-shot mode for debugging only.

## 3. Output Contract

- [ ] Add normalized success/error envelope helpers.
- [ ] Add `ue_cli_tool.formatter` for text/json/raw output modes.
- [ ] Make text output the default CLI output mode.
- [ ] Add explicit `--json` and `--raw` flags.
- [ ] Map transport failures to stable error codes.
- [ ] Map parse failures to stable error codes.
- [ ] Preserve `_cli_line` and batch child errors.
- [ ] Add command-specific text summaries for high-volume result types.
- [ ] Keep verbose/raw output behind explicit flags.

## 4. Skill Migration

- [ ] Create a Codex skill for direct UE CLI usage.
- [ ] Teach command discovery through `ue query help` and `ue query search`.
- [ ] Teach health/debug flow through `ue doctor` and `ue query logs`.
- [ ] Mark existing MCP skill usage as legacy fallback.

## 5. Tests

- [ ] Add unit tests for daemon request/response framing.
- [ ] Add tests for daemon lifecycle commands.
- [ ] Add CLI contract tests for `run`, `query`, and `doctor`.
- [ ] Add formatter contract tests for text, json, raw, errors, and batch
      output.
- [ ] Add regression tests proving CLI subprocesses do not reconnect to UE for
      every command when daemon mode is active.
- [ ] Keep existing MCP tests until migration is complete.

## 6. Documentation

- [ ] Update architecture docs to show CLI-first path.
- [ ] Update installation docs to configure CLI/daemon by default.
- [ ] Move MCP setup docs to legacy section.
- [ ] Document ports: `tcp_port` for UE bridge, `daemon_port` for Python IPC.
