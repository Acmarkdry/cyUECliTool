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
