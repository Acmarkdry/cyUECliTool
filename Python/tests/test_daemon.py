# coding: utf-8
from __future__ import annotations

from ue_cli_tool.config import ProjectConfig
from ue_cli_tool.daemon import UEDaemon


class _FakeConnection:
	def __init__(self):
		self.is_connected = True
		self.on_state_change = None

	def connect(self):
		self.is_connected = True
		return True

	def disconnect(self):
		self.is_connected = False

	def send_command(self, command_type, params=None):
		class _Result:
			def to_dict(self_inner):
				return {"success": True, "command": command_type, "params": params or {}}

		return _Result()

	def get_health(self):
		return {"connection_state": "connected", "is_connected": True}

	def ping(self):
		return True


class _FakeContext:
	def _on_ue_state_change(self, *args, **kwargs):
		pass

	def shutdown(self):
		pass

	def record_operation(self, *args, **kwargs):
		pass

	def track_assets(self, *args, **kwargs):
		pass

	def get_status(self):
		return {"ue_connection": "alive"}


def _daemon(monkeypatch):
	monkeypatch.setattr("ue_cli_tool.daemon.ContextStore", lambda *a, **k: _FakeContext())
	monkeypatch.setattr("ue_cli_tool.daemon.PersistentUnrealConnection", lambda *a, **k: _FakeConnection())
	monkeypatch.setattr("ue_cli_tool.daemon._wire_metrics", lambda *a, **k: None)
	return UEDaemon(ProjectConfig(tcp_port=55558, daemon_port=55559))


def test_daemon_run_returns_envelope(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "run", "command": "get_context"})

	assert response["success"] is True
	assert response["action"] == "get_context"
	assert response["data"]["command"] == "get_context"


def test_daemon_query_health_uses_owned_connection(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "query", "query": "health"})

	assert response["success"] is True
	assert response["data"]["health"]["is_connected"] is True


def test_daemon_unknown_request_is_error(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "wat"})

	assert response["success"] is False
	assert response["error"]["code"] == "UNKNOWN_DAEMON_REQUEST"
