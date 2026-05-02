# coding: utf-8
"""Text-first result formatting for the UE CLI runtime."""

from __future__ import annotations

import json
from typing import Any

IMPORTANT_KEYS = (
	"name",
	"asset_path",
	"path",
	"blueprint_name",
	"material_name",
	"widget_name",
	"graph_name",
	"node_id",
	"status",
	"message",
	"description",
	"total",
	"count",
	"node_count",
	"pin_count",
	"warning_count",
	"error_count",
	"version",
)


def make_result(
	action: str,
	data: Any,
	*,
	cli_line: str | None = None,
	diagnostics: dict[str, Any] | None = None,
) -> dict[str, Any]:
	return {
		"success": True,
		"kind": "result",
		"action": action,
		"cli_line": cli_line,
		"data": data if data is not None else {},
		"diagnostics": diagnostics or {},
	}


def make_error(
	code: str,
	message: str,
	*,
	action: str | None = None,
	recoverable: bool = True,
	suggested_next: str | None = None,
	diagnostics: dict[str, Any] | None = None,
) -> dict[str, Any]:
	return {
		"success": False,
		"kind": "error",
		"action": action or "",
		"error": {
			"code": code,
			"message": message,
			"recoverable": recoverable,
			"suggested_next": suggested_next or "",
		},
		"diagnostics": diagnostics or {},
	}


def envelope_from_result(action: str, result: Any, *, cli_line: str | None = None) -> dict[str, Any]:
	if not isinstance(result, dict):
		return make_result(action, result, cli_line=cli_line)

	if result.get("success") is False:
		return make_error(
			_error_code_from_result(result),
			str(result.get("error", "Command failed")),
			action=action,
			recoverable=bool(result.get("recoverable", True)),
			suggested_next=_suggest_next(action, result),
			diagnostics=_diagnostics_from_result(result),
		)

	data = {k: v for k, v in result.items() if k not in ("success", "_cli_line")}
	return make_result(action, data, cli_line=cli_line or result.get("_cli_line"))


def format_output(envelope: dict[str, Any], mode: str = "text") -> str:
	mode = (mode or "text").lower()
	if mode == "json":
		return json.dumps(envelope, indent=2, ensure_ascii=False)
	if mode == "raw":
		return json.dumps(envelope.get("data", envelope), indent=2, ensure_ascii=False)
	return format_text(envelope)


def format_text(envelope: dict[str, Any]) -> str:
	if not envelope.get("success", False):
		return _format_error(envelope)

	data = envelope.get("data")
	action = envelope.get("action") or "command"

	if isinstance(data, dict):
		if action == "exec_python":
			return _format_exec_python(data)
		if action == "doctor":
			return _format_doctor(data)
		if action == "daemon.status":
			return _format_daemon_status(data)
		if "health" in data and isinstance(data["health"], dict):
			return _format_health(action, data["health"])
		if "help" in data and isinstance(data["help"], str):
			return f"OK {action}\n{data['help'].strip()}"
		if "domains" in data:
			return _format_help_domains(data)
		if "results" in data and _looks_like_batch(data):
			return _format_batch(action, data)
		if "results" in data and isinstance(data["results"], list):
			return _format_result_list(action, data["results"], data)
		if "skills" in data and isinstance(data["skills"], list):
			return _format_result_list(action, data["skills"], data, title_key="skill_id")

	lines = [f"OK {action}"]
	lines.extend(_summarize_value(data))
	if len(lines) == 1:
		lines.append("Result: ok")
	return "\n".join(lines)


def _format_exec_python(data: dict[str, Any]) -> str:
	lines = ["OK exec_python"]
	stdout = str(data.get("stdout") or "").strip()
	stderr = str(data.get("stderr") or "").strip()
	return_value = data.get("return_value")

	if stdout:
		lines.append(f"Stdout: {_short(stdout)}")
	if stderr:
		lines.append(f"Stderr: {_short(stderr)}")
	if "return_value" in data:
		lines.append(f"Return value: {_short(return_value)}")
	if len(lines) == 1:
		lines.append("Result: ok")
	return "\n".join(lines)


def _format_doctor(data: dict[str, Any]) -> str:
	daemon = data.get("daemon") if isinstance(data.get("daemon"), dict) else {}
	unreal = data.get("unreal") if isinstance(data.get("unreal"), dict) else {}
	config = data.get("config") if isinstance(data.get("config"), dict) else {}
	source = data.get("source") if isinstance(data.get("source"), dict) else {}
	health = unreal.get("health") if isinstance(unreal.get("health"), dict) else {}

	lines = ["OK doctor"]
	if source.get("version"):
		lines.append(f"Version: {source.get('version')}")
	lines.append(
		"Daemon: "
		+ _status_word(bool(daemon.get("running")))
		+ f" pid={daemon.get('pid', '?')} port={daemon.get('port', '?')}"
	)
	lines.append(
		"Unreal: "
		+ _status_word(bool(unreal.get("port_open")))
		+ f" port={unreal.get('port', '?')} connected={_yes_no(bool(health.get('is_connected')))}"
	)
	state = health.get("connection_state")
	if state:
		lines.append(f"UE state: {state}")
	last_error = health.get("last_error")
	if last_error:
		lines.append(f"Last UE error: {_short(last_error)}")
	if config.get("project_root"):
		lines.append(f"Project: {config.get('project_root')}")
	if config.get("engine_root"):
		lines.append(f"Engine: {config.get('engine_root')}")
	lines.append(f"Auto-start daemon: {_yes_no(bool(config.get('auto_start_daemon')))}")
	return "\n".join(lines)


def _format_daemon_status(data: dict[str, Any]) -> str:
	source = data.get("source") if isinstance(data.get("source"), dict) else {}
	health = data.get("ue_health") if isinstance(data.get("ue_health"), dict) else {}
	context = data.get("context") if isinstance(data.get("context"), dict) else {}

	lines = ["OK daemon.status"]
	if source.get("version"):
		lines.append(f"Version: {source.get('version')}")
	lines.append(f"PID: {data.get('pid', '?')}")
	lines.append(f"Daemon: {data.get('host', '127.0.0.1')}:{data.get('daemon_port', '?')}")
	lines.append(
		f"Unreal: {data.get('host', '127.0.0.1')}:{data.get('tcp_port', '?')} "
		f"connected={_yes_no(bool(health.get('is_connected')))}"
	)
	if health.get("connection_state"):
		lines.append(f"UE state: {health.get('connection_state')}")
	if health.get("last_error"):
		lines.append(f"Last UE error: {_short(health.get('last_error'))}")
	if context.get("op_count") is not None:
		lines.append(f"Operations: {context.get('op_count')}")
	if data.get("project_root"):
		lines.append(f"Project: {data.get('project_root')}")
	if source.get("python_executable"):
		lines.append(f"Python: {source.get('python_executable')}")
	return "\n".join(lines)


def _format_health(action: str, health: dict[str, Any]) -> str:
	config = health.get("config") if isinstance(health.get("config"), dict) else {}
	circuit = health.get("circuit_breaker") if isinstance(health.get("circuit_breaker"), dict) else {}

	lines = [f"OK {action}"]
	lines.append(f"Connected: {_yes_no(bool(health.get('is_connected')))}")
	if health.get("connection_state"):
		lines.append(f"State: {health.get('connection_state')}")
	if config:
		lines.append(f"UE endpoint: {config.get('host', '127.0.0.1')}:{config.get('port', '?')}")
	if circuit.get("state"):
		lines.append(f"Circuit breaker: {circuit.get('state')}")
	if health.get("consecutive_failures") is not None:
		lines.append(f"Consecutive failures: {health.get('consecutive_failures')}")
	if health.get("last_error"):
		lines.append(f"Last error: {_short(health.get('last_error'))}")
	return "\n".join(lines)


def _format_error(envelope: dict[str, Any]) -> str:
	err = envelope.get("error") or {}
	code = err.get("code", "ERROR") if isinstance(err, dict) else "ERROR"
	msg = err.get("message", str(err)) if isinstance(err, dict) else str(err)
	recoverable = err.get("recoverable", True) if isinstance(err, dict) else True
	next_step = err.get("suggested_next", "") if isinstance(err, dict) else ""

	lines = [f"ERROR {code}", msg]
	lines.append(f"Recoverable: {'yes' if recoverable else 'no'}")
	if next_step:
		lines.append(f"Next: {next_step}")
	return "\n".join(lines)


def _format_batch(action: str, data: dict[str, Any]) -> str:
	results = data.get("results") or []
	total = int(data.get("total", len(results)) or len(results))
	ok_count = sum(1 for item in results if isinstance(item, dict) and item.get("success") is not False)
	status = "OK" if data.get("success", True) and ok_count == len(results) else "ERROR"
	lines = [f"{status} batch {ok_count}/{total}"]
	for idx, item in enumerate(results, start=1):
		if not isinstance(item, dict):
			lines.append(f"{idx} OK result")
			continue
		child_status = "OK" if item.get("success") is not False else "ERROR"
		label = item.get("_cli_line") or item.get("command") or item.get("type") or "command"
		if child_status == "ERROR":
			lines.append(f"{idx} ERROR {label}: {item.get('error', 'failed')}")
		else:
			lines.append(f"{idx} OK {label}")
	return "\n".join(lines)


def _format_help_domains(data: dict[str, Any]) -> str:
	lines = [f"OK help: {data.get('total', 0)} commands"]
	domains = data.get("domains") or {}
	for domain in sorted(domains):
		commands = domains[domain]
		if not isinstance(commands, list):
			continue
		names = [str(c.get("command", c)) for c in commands[:8] if isinstance(c, dict)]
		suffix = f" (+{len(commands) - 8} more)" if len(commands) > 8 else ""
		lines.append(f"{domain}: {', '.join(names)}{suffix}")
	lines.append("Next: ue query \"help <command>\"")
	return "\n".join(lines)


def _format_result_list(
	action: str,
	results: list[Any],
	data: dict[str, Any],
	*,
	title_key: str = "command",
) -> str:
	total = data.get("total", len(results))
	lines = [f"OK {action}: {len(results)}/{total} results"]
	for idx, item in enumerate(results[:12], start=1):
		if isinstance(item, dict):
			title = item.get(title_key) or item.get("id") or item.get("name") or item.get("path") or f"item_{idx}"
			desc = item.get("description") or item.get("error") or ""
			lines.append(f"{idx}. {title}{': ' + str(desc)[:120] if desc else ''}")
		else:
			lines.append(f"{idx}. {str(item)[:140]}")
	if len(results) > 12:
		lines.append(f"... {len(results) - 12} more")
	return "\n".join(lines)


def _summarize_value(value: Any, *, indent: str = "") -> list[str]:
	if isinstance(value, dict):
		return _summarize_dict(value, indent=indent)
	if isinstance(value, list):
		return _summarize_list(value, indent=indent)
	if value is None:
		return []
	return [f"{indent}Result: {str(value)[:300]}"]


def _summarize_dict(data: dict[str, Any], *, indent: str = "") -> list[str]:
	lines: list[str] = []
	for key in IMPORTANT_KEYS:
		if key in data:
			lines.append(f"{indent}{_label(key)}: {_short(data[key])}")

	for key, value in data.items():
		if key.startswith("_") or key in IMPORTANT_KEYS:
			continue
		if len(lines) >= 12:
			lines.append(f"{indent}... {len(data) - len(lines)} more fields. Use --json for details.")
			break
		if isinstance(value, list):
			lines.append(f"{indent}{_label(key)}: {len(value)} items")
		elif isinstance(value, dict):
			lines.append(f"{indent}{_label(key)}: {len(value)} fields")
		else:
			lines.append(f"{indent}{_label(key)}: {_short(value)}")
	return lines


def _summarize_list(items: list[Any], *, indent: str = "") -> list[str]:
	lines = [f"{indent}Items: {len(items)}"]
	for idx, item in enumerate(items[:8], start=1):
		if isinstance(item, dict):
			name = item.get("name") or item.get("path") or item.get("command") or item.get("id") or f"item_{idx}"
			lines.append(f"{indent}{idx}. {name}")
		else:
			lines.append(f"{indent}{idx}. {_short(item)}")
	if len(items) > 8:
		lines.append(f"{indent}... {len(items) - 8} more. Use --json for details.")
	return lines


def _looks_like_batch(data: dict[str, Any]) -> bool:
	return "executed" in data or any(
		isinstance(item, dict) and "_cli_line" in item for item in data.get("results", [])
	)


def _short(value: Any, limit: int = 220) -> str:
	if isinstance(value, (list, dict)):
		text = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
	else:
		text = str(value)
	text = text.replace("\r", " ").replace("\n", " ")
	return text if len(text) <= limit else text[: limit - 3] + "..."


def _label(key: str) -> str:
	return key.replace("_", " ").capitalize()


def _yes_no(value: bool) -> str:
	return "yes" if value else "no"


def _status_word(value: bool) -> str:
	return "running" if value else "stopped"


def _diagnostics_from_result(result: dict[str, Any]) -> dict[str, Any]:
	return {
		k: v
		for k, v in result.items()
		if k not in ("success", "error", "error_type", "recoverable")
	}


def _error_code_from_result(result: dict[str, Any]) -> str:
	error_type = str(result.get("error_type", "")).upper()
	if error_type:
		return error_type.replace(" ", "_")
	error = str(result.get("error", "")).lower()
	if "connect" in error or "socket" in error or "timed out" in error:
		return "UE_TRANSPORT_ERROR"
	if "parse" in error:
		return "PARSE_ERROR"
	return "COMMAND_FAILED"


def _suggest_next(action: str, result: dict[str, Any]) -> str:
	error = str(result.get("error", "")).lower()
	if "connect" in error or "socket" in error:
		return "Run `ue doctor` and verify Unreal Editor is open with the configured port."
	if "parse" in error:
		return f"Run `ue query \"help {action}\"` to check command syntax."
	return "Run the command with --json for full diagnostics."
