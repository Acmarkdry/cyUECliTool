# coding: utf-8
from __future__ import annotations

from ue_cli_tool import cli


def test_bare_command_is_treated_as_run(monkeypatch, capsys):
	def fake_request(payload, config, args):
		assert payload == {"type": "run", "command": "get_context"}
		return {"success": True, "kind": "result", "action": "get_context", "data": {"name": "ctx"}}

	monkeypatch.setattr(cli, "_request_with_autostart", fake_request)

	rc = cli.main(["get_context"])
	out = capsys.readouterr().out

	assert rc == 0
	assert "OK get_context" in out


def test_query_json_flag(monkeypatch, capsys):
	def fake_request(payload, config, args):
		assert payload == {"type": "query", "query": "health"}
		return {"success": True, "kind": "result", "action": "query health", "data": {"health": {"ok": True}}}

	monkeypatch.setattr(cli, "_request_with_autostart", fake_request)

	rc = cli.main(["query", "--json", "health"])
	out = capsys.readouterr().out

	assert rc == 0
	assert '"query health"' in out
	assert out.lstrip().startswith("{")
