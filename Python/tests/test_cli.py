# coding: utf-8
from __future__ import annotations

import io

from ue_cli_tool import cli
from ue_cli_tool.config import ProjectConfig


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


def test_run_file_sends_command_text(monkeypatch, tmp_path, capsys):
	script = tmp_path / "commands.uecli"
	script.write_text("\ufeff@BP_Player\nget_blueprint_summary --detail_level brief\n", encoding="utf-8")

	def fake_request(payload, config, args):
		assert payload == {
			"type": "run",
			"command": "@BP_Player\nget_blueprint_summary --detail_level brief\n",
		}
		return {"success": True, "kind": "result", "action": "batch_execute", "data": {"results": []}}

	monkeypatch.setattr(cli, "_request_with_autostart", fake_request)

	rc = cli.main(["run", "--file", str(script)])
	out = capsys.readouterr().out

	assert rc == 0
	assert "OK batch_execute" in out


def test_run_file_read_error(monkeypatch, capsys):
	def fail_request(payload, config, args):
		raise AssertionError("request should not be sent")

	monkeypatch.setattr(cli, "_request_with_autostart", fail_request)

	rc = cli.main(["run", "--file", "does-not-exist.uecli"])
	out = capsys.readouterr().out

	assert rc == 1
	assert "RUN_FILE_READ_FAILED" in out


def test_python_positional_sends_exec_python(monkeypatch, capsys):
	def fake_request(payload, config, args):
		assert payload == {"type": "exec_python", "code": "print('hello')"}
		return {"success": True, "kind": "result", "action": "exec_python", "data": {"stdout": "hello\n"}}

	monkeypatch.setattr(cli, "_request_with_autostart", fake_request)

	rc = cli.main(["python", "print('hello')"])
	out = capsys.readouterr().out

	assert rc == 0
	assert "OK exec_python" in out


def test_py_alias_reads_stdin(monkeypatch, capsys):
	stdin = io.StringIO("import unreal\n_result = 7\n")

	def fake_request(payload, config, args):
		assert payload == {"type": "exec_python", "code": "import unreal\n_result = 7\n"}
		return {"success": True, "kind": "result", "action": "exec_python", "data": {"return_value": 7}}

	monkeypatch.setattr(cli.sys, "stdin", stdin)
	monkeypatch.setattr(cli, "_request_with_autostart", fake_request)

	rc = cli.main(["py"])
	out = capsys.readouterr().out

	assert rc == 0
	assert "Return value: 7" in out


def test_python_file_sends_file_contents(monkeypatch, tmp_path, capsys):
	script = tmp_path / "script.py"
	script.write_text("\ufeff_result = {'ok': True}\n", encoding="utf-8")

	def fake_request(payload, config, args):
		assert payload == {"type": "exec_python", "code": "_result = {'ok': True}\n"}
		return {"success": True, "kind": "result", "action": "exec_python", "data": {"return_value": {"ok": True}}}

	monkeypatch.setattr(cli, "_request_with_autostart", fake_request)

	rc = cli.main(["python", "--file", str(script), "--json"])
	out = capsys.readouterr().out

	assert rc == 0
	assert '"exec_python"' in out
	assert '"ok": true' in out


def test_python_empty_code_is_error(monkeypatch, capsys):
	stdin = io.StringIO("")

	def fail_request(payload, config, args):
		raise AssertionError("request should not be sent")

	monkeypatch.setattr(cli.sys, "stdin", stdin)
	monkeypatch.setattr(cli, "_request_with_autostart", fail_request)

	rc = cli.main(["python"])
	out = capsys.readouterr().out

	assert rc == 1
	assert "PYTHON_CODE_REQUIRED" in out


def test_start_daemon_closes_inherited_handles(monkeypatch):
	checks = iter([False, True])
	popen_kwargs = {}

	def fake_can_connect(port, timeout=0.5):
		return next(checks, True)

	class FakePopen:
		def __init__(self, args, **kwargs):
			popen_kwargs.update(kwargs)

	monkeypatch.setattr(cli, "can_connect", fake_can_connect)
	monkeypatch.setattr(cli.subprocess, "Popen", FakePopen)

	started = cli._start_daemon(ProjectConfig(tcp_port=55558, daemon_port=55559))

	assert started is True
	assert popen_kwargs["stdin"] is cli.subprocess.DEVNULL
	assert popen_kwargs["stdout"] is cli.subprocess.DEVNULL
	assert popen_kwargs["stderr"] is cli.subprocess.DEVNULL
	assert popen_kwargs["close_fds"] is True
