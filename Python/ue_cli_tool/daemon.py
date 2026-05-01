# coding: utf-8
"""Local daemon that owns the persistent Unreal Editor connection."""

from __future__ import annotations

import json
import logging
import os
import socket
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .config import ProjectConfig, load_config
from .connection import ConnectionConfig, PersistentUnrealConnection, _wire_metrics
from .context import ContextStore
from .formatter import envelope_from_result, make_error, make_result
from .ipc import recv_json, send_json
from . import runtime

logger = logging.getLogger(__name__)

HOST = "127.0.0.1"


def context_dir() -> Path:
	return Path(__file__).parent.parent.parent / ".context"


def status_path() -> Path:
	return context_dir() / "ue_daemon.json"


class UEDaemon:
	def __init__(self, config: ProjectConfig | None = None):
		self.config = config or load_config()
		self.context = ContextStore(context_dir())
		self.connection = PersistentUnrealConnection(ConnectionConfig(port=self.config.tcp_port))
		_wire_metrics(self.connection)
		self.connection.on_state_change = self.context._on_ue_state_change
		self._stop = threading.Event()
		self._server_socket: socket.socket | None = None
		self._started_at = datetime.now(timezone.utc).isoformat()

	def serve_forever(self) -> int:
		runtime.init_runtime(context_dir=context_dir())
		self._write_status("starting")
		self.connection.connect()

		with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
			server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
			server.bind((HOST, self.config.daemon_port))
			server.listen(16)
			server.settimeout(0.5)
			self._server_socket = server
			self._write_status("running")
			logger.info("UE CLI daemon listening on %s:%s", HOST, self.config.daemon_port)

			while not self._stop.is_set():
				try:
					client, _addr = server.accept()
				except socket.timeout:
					continue
				except OSError:
					break
				thread = threading.Thread(target=self._handle_client, args=(client,), daemon=True)
				thread.start()

		self.shutdown()
		return 0

	def shutdown(self) -> None:
		self._stop.set()
		try:
			if self._server_socket is not None:
				self._server_socket.close()
		except Exception:
			pass
		try:
			self.connection.disconnect()
		except Exception:
			pass
		try:
			self.context.shutdown()
		except Exception:
			pass
		self._write_status("stopped")

	def _handle_client(self, client: socket.socket) -> None:
		with client:
			try:
				request = recv_json(client)
				if request is None:
					return
				response = self.handle_request(request)
			except Exception as exc:
				logger.exception("Daemon request failed")
				response = make_error(
					"DAEMON_ERROR",
					str(exc),
					recoverable=True,
					suggested_next="Run `ue doctor` and inspect daemon logs.",
				)
			send_json(client, response)

	def handle_request(self, request: dict[str, Any]) -> dict[str, Any]:
		req_type = str(request.get("type", "")).lower()
		if req_type == "ping":
			return make_result("daemon.ping", self._status_payload(extra_health=False))
		if req_type == "status":
			return make_result("daemon.status", self._status_payload(extra_health=True))
		if req_type == "stop":
			self._stop.set()
			return make_result("daemon.stop", {"stopping": True})
		if req_type == "doctor":
			return make_result("doctor", self._doctor_payload())
		if req_type == "run":
			command = str(request.get("command", ""))
			result = runtime.handle_cli(
				{"command": command},
				send_command_func=self._send_command,
				log_command_func=self._log_command,
			)
			action = _infer_action(command, fallback="run")
			return envelope_from_result(action, result)
		if req_type == "query":
			query = str(request.get("query", ""))
			result = runtime.handle_query(
				{"query": query},
				send_command_func=self._send_command,
				connection_health_func=self.connection.get_health,
				ping_func=self.connection.ping,
			)
			if isinstance(result, str):
				result = {"success": True, "text": result}
			return envelope_from_result(f"query {query}".strip(), result)
		return make_error(
			"UNKNOWN_DAEMON_REQUEST",
			f"Unknown daemon request type: {req_type}",
			recoverable=False,
		)

	def _send_command(self, command_type: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
		if not self.connection.is_connected:
			self.connection.connect()
		return self.connection.send_command(command_type, params).to_dict()

	def _log_command(self, action_id: str, params: dict | None, result: dict, elapsed_ms: float) -> None:
		runtime._log_command(action_id, params, result, elapsed_ms)
		try:
			self.context.record_operation(action_id, action_id, params, bool(result.get("success", False)), result, elapsed_ms)
			self.context.track_assets(params, action_id)
		except Exception:
			logger.warning("Daemon context recording failed", exc_info=True)

	def _status_payload(self, *, extra_health: bool) -> dict[str, Any]:
		payload = {
			"pid": os.getpid(),
			"host": HOST,
			"daemon_port": self.config.daemon_port,
			"tcp_port": self.config.tcp_port,
			"project_root": self.config.project_root,
			"engine_root": self.config.engine_root,
			"started_at": self._started_at,
			"status_path": str(status_path()),
		}
		if extra_health:
			payload["ue_health"] = self.connection.get_health()
			payload["context"] = self.context.get_status()
		return payload

	def _doctor_payload(self) -> dict[str, Any]:
		daemon_port_open = can_connect(self.config.daemon_port, timeout=0.25)
		ue_port_open = can_connect(self.config.tcp_port, timeout=0.25)
		health = self.connection.get_health()
		return {
			"daemon": {
				"running": True,
				"port_open": daemon_port_open,
				"port": self.config.daemon_port,
				"pid": os.getpid(),
			},
			"unreal": {
				"port_open": ue_port_open,
				"port": self.config.tcp_port,
				"health": health,
			},
			"config": {
				"project_root": self.config.project_root,
				"engine_root": self.config.engine_root,
				"auto_start_daemon": self.config.auto_start_daemon,
			},
		}

	def _write_status(self, state: str) -> None:
		path = status_path()
		path.parent.mkdir(parents=True, exist_ok=True)
		payload = {
			"state": state,
			"pid": os.getpid(),
			"host": HOST,
			"daemon_port": self.config.daemon_port,
			"tcp_port": self.config.tcp_port,
			"project_root": self.config.project_root,
			"started_at": self._started_at,
			"updated_at": datetime.now(timezone.utc).isoformat(),
		}
		try:
			path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
		except Exception:
			logger.warning("Failed to write daemon status", exc_info=True)


def can_connect(port: int, *, timeout: float = 0.5) -> bool:
	try:
		with socket.create_connection((HOST, port), timeout=timeout):
			return True
	except OSError:
		return False


def request_daemon(payload: dict[str, Any], *, config: ProjectConfig | None = None, timeout: float = 30.0) -> dict[str, Any]:
	cfg = config or load_config()
	with socket.create_connection((HOST, cfg.daemon_port), timeout=timeout) as sock:
		sock.settimeout(timeout)
		send_json(sock, payload)
		response = recv_json(sock)
	if response is None:
		return make_error("DAEMON_EMPTY_RESPONSE", "Daemon closed connection without a response")
	return response


def _infer_action(command_text: str, *, fallback: str) -> str:
	for line in command_text.splitlines():
		stripped = line.strip()
		if not stripped or stripped.startswith("#") or stripped.startswith("@"):
			continue
		return stripped.split()[0]
	return fallback


def main() -> int:
	logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
	return UEDaemon().serve_forever()


if __name__ == "__main__":
	raise SystemExit(main())
