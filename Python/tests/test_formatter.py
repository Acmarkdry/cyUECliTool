# coding: utf-8
from __future__ import annotations

import json

from ue_cli_tool.formatter import envelope_from_result, format_output, make_error


def test_success_defaults_to_text_not_json():
	envelope = envelope_from_result(
		"get_context",
		{"success": True, "asset_path": "/Game/BP_Player", "selected": ["A", "B"]},
	)

	text = format_output(envelope)

	assert text.startswith("OK get_context")
	assert "/Game/BP_Player" in text
	assert not text.lstrip().startswith("{")


def test_error_text_has_code_and_next_step():
	envelope = make_error(
		"UE_NOT_CONNECTED",
		"Cannot connect to Unreal Editor",
		suggested_next="Run `ue doctor`.",
	)

	text = format_output(envelope)

	assert "ERROR UE_NOT_CONNECTED" in text
	assert "Cannot connect" in text
	assert "Next: Run `ue doctor`." in text


def test_json_mode_returns_envelope():
	envelope = envelope_from_result("ping", {"success": True, "pong": True})

	raw = format_output(envelope, "json")
	decoded = json.loads(raw)

	assert decoded["success"] is True
	assert decoded["data"]["pong"] is True


def test_cli_line_is_only_in_envelope_not_data():
	envelope = envelope_from_result(
		"exec_python",
		{"success": True, "return_value": 7, "_cli_line": "exec_python _result = 7"},
	)

	assert envelope["cli_line"] == "exec_python _result = 7"
	assert "_cli_line" not in envelope["data"]


def test_exec_python_text_omits_empty_streams_and_shows_value():
	envelope = envelope_from_result(
		"exec_python",
		{"success": True, "stdout": "", "stderr": "", "return_value": {"x": 1}},
	)

	text = format_output(envelope)

	assert text == 'OK exec_python\nReturn value: {"x":1}'


def test_exec_python_text_suppresses_duplicate_stdout_value():
	envelope = envelope_from_result(
		"exec_python",
		{"success": True, "stdout": '{"x":1}\n', "stderr": "", "return_value": {"x": 1}},
	)

	text = format_output(envelope)

	assert text == 'OK exec_python\nReturn value: {"x":1}'


def test_batch_output_is_line_oriented():
	envelope = envelope_from_result(
		"batch_execute",
		{
			"success": True,
			"total": 2,
			"executed": 2,
			"results": [
				{"success": True, "_cli_line": "create_blueprint BP_A"},
				{"success": True, "_cli_line": "compile_blueprint BP_A"},
			],
		},
	)

	text = format_output(envelope)

	assert "OK batch 2/2" in text
	assert "1 OK create_blueprint BP_A" in text
	assert "2 OK compile_blueprint BP_A" in text


def test_batch_error_output_shows_executed_and_skipped_lines():
	envelope = envelope_from_result(
		"run",
		{
			"success": False,
			"error": "Command failed: no_such_command: Unknown command",
			"error_type": "parse_error",
			"total": 3,
			"executed": 2,
			"skipped": 1,
			"stopped_on_error": True,
			"results": [
				{"success": True, "_cli_line": "ping"},
				{"success": False, "error": "Unknown command", "_cli_line": "no_such_command"},
			],
		},
	)

	text = format_output(envelope)

	assert "ERROR PARSE_ERROR" in text
	assert "ERROR batch 1/3" in text
	assert "1 OK ping" in text
	assert "2 ERROR no_such_command: Unknown command" in text


def test_health_output_shows_connection_state():
	envelope = envelope_from_result(
		"query health",
		{
			"success": True,
			"health": {
				"is_connected": True,
				"connection_state": "connected",
				"consecutive_failures": 0,
				"config": {"host": "127.0.0.1", "port": 55558},
				"circuit_breaker": {"state": "closed"},
			},
		},
	)

	text = format_output(envelope)

	assert "OK query health" in text
	assert "Connected: yes" in text
	assert "UE endpoint: 127.0.0.1:55558" in text


def test_health_error_output_keeps_connection_details():
	envelope = envelope_from_result(
		"query health",
		{
			"success": False,
			"error": "Unreal Editor is not connected",
			"error_type": "ue_not_connected",
			"health": {"is_connected": False, "connection_state": "disconnected"},
		},
	)

	text = format_output(envelope)

	assert "ERROR UE_NOT_CONNECTED" in text
	assert "Connected: no" in text
	assert "State: disconnected" in text


def test_doctor_output_is_actionable():
	envelope = envelope_from_result(
		"doctor",
		{
			"success": True,
			"daemon": {"running": True, "pid": 123, "port": 55559},
			"unreal": {
				"port_open": True,
				"port": 55558,
				"health": {"is_connected": True, "connection_state": "connected"},
			},
			"config": {
				"project_root": "D:/Project",
				"engine_root": "D:/UE",
				"auto_start_daemon": True,
			},
			"source": {"version": "0.6.0"},
		},
	)

	text = format_output(envelope)

	assert "OK doctor" in text
	assert "Version: 0.6.0" in text
	assert "Daemon: running pid=123 port=55559" in text
	assert "Unreal: running port=55558 connected=yes" in text
	assert "Project: D:/Project" in text
