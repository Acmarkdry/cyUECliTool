# coding: utf-8
"""Command-line entrypoint for the UE CLI-first runtime."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from . import __version__
from .config import ProjectConfig, load_config
from .daemon import HOST, can_connect, request_daemon
from .formatter import format_output, make_error, make_result


def main(argv: list[str] | None = None) -> int:
	argv = list(sys.argv[1:] if argv is None else argv)
	if argv == ["--version"]:
		print(f"ue {__version__}")
		return 0
	if argv and argv[0] not in ("run", "query", "daemon", "doctor", "python", "py", "version", "-h", "--help", "--version"):
		argv = ["run", *argv]

	parser = _build_parser()
	args = parser.parse_args(argv)
	config = load_config()

	if args.command == "run":
		try:
			text = _run_command_text(args)
		except OSError as exc:
			response = make_error("RUN_FILE_READ_FAILED", str(exc), recoverable=False)
			return _print_response(response, args)
		return _print_response(
			_request_with_autostart(
				{"type": "run", "command": text, "continue_on_error": bool(args.continue_on_error)},
				config,
				args,
			),
			args,
		)
	if args.command == "query":
		text = " ".join(args.query_text).strip()
		return _print_response(_request_with_autostart({"type": "query", "query": text}, config, args), args)
	if args.command in ("python", "py"):
		try:
			code = _python_code(args)
		except OSError as exc:
			response = make_error("PYTHON_FILE_READ_FAILED", str(exc), recoverable=False)
			return _print_response(response, args)
		if not code.strip():
			response = make_error(
				"PYTHON_CODE_REQUIRED",
				"Python code is required. Pass code, pipe stdin, or use --file.",
				recoverable=False,
			)
			return _print_response(response, args)
		return _print_response(_request_with_autostart({"type": "exec_python", "code": code}, config, args), args)
	if args.command == "doctor":
		response = _request_with_autostart({"type": "doctor"}, config, args)
		return _print_response(response, args)
	if args.command == "version":
		return _print_response(make_result("version", {"version": __version__}), args)
	if args.command == "daemon":
		return _handle_daemon(args, config)

	parser.print_help()
	return 1


def _build_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(prog="ue", description="CLI-first Unreal Editor automation")
	parser.add_argument("--version", action="version", version=f"ue {__version__}")
	sub = parser.add_subparsers(dest="command")

	run = sub.add_parser("run", help="Execute UE CLI command text")
	_add_output_flags(run)
	run.add_argument("command_text", nargs="*", help="Command text. Reads stdin when omitted.")
	run.add_argument("--file", "-f", dest="command_file", help="Read UE CLI command text from a UTF-8 file.")
	run.add_argument(
		"--continue-on-error",
		action="store_true",
		help="Keep executing a multi-command run batch after a command fails.",
	)
	run.add_argument("--no-daemon", action="store_true", help="Do not auto-start the daemon.")

	query = sub.add_parser("query", help="Query help, search, logs, metrics, health, skills, resources")
	_add_output_flags(query)
	query.add_argument("query_text", nargs="*", help="Query text, for example: help create_blueprint")
	query.add_argument("--no-daemon", action="store_true", help="Do not auto-start the daemon.")

	python = sub.add_parser("python", aliases=["py"], help="Execute Unreal Python code without CLI command parsing")
	_add_output_flags(python)
	python.add_argument("python_code", nargs="*", help="Python code. Reads stdin when omitted.")
	python.add_argument("--file", "-f", dest="python_file", help="Read Python code from a UTF-8 file.")
	python.add_argument("--no-daemon", action="store_true", help="Do not auto-start the daemon.")

	doctor = sub.add_parser("doctor", help="Run local diagnostics")
	_add_output_flags(doctor)
	doctor.add_argument("--no-daemon", action="store_true", help="Do not auto-start the daemon.")

	version = sub.add_parser("version", help="Print CLI version")
	_add_output_flags(version)

	daemon = sub.add_parser("daemon", help="Manage the local UE CLI daemon")
	daemon_sub = daemon.add_subparsers(dest="daemon_command")
	start = daemon_sub.add_parser("start", help="Start daemon in the background")
	start.add_argument("--restart", action="store_true", help="Stop a running daemon before starting.")
	daemon_sub.add_parser("serve", help="Run daemon in the foreground")
	daemon_sub.add_parser("stop", help="Stop daemon")
	daemon_sub.add_parser("restart", help="Restart daemon and reload Python modules")
	status = daemon_sub.add_parser("status", help="Show daemon status")
	_add_output_flags(status)
	return parser


def _add_output_flags(parser: argparse.ArgumentParser) -> None:
	group = parser.add_mutually_exclusive_group()
	group.add_argument("--json", action="store_true", help="Print machine-readable JSON envelope")
	group.add_argument("--raw", action="store_true", help="Print raw data payload")


def _command_text(parts: list[str]) -> str:
	if parts:
		return " ".join(parts)
	if not sys.stdin.isatty():
		return _strip_leading_bom(sys.stdin.read())
	return ""


def _run_command_text(args: argparse.Namespace) -> str:
	command_file = getattr(args, "command_file", None)
	if command_file:
		return _read_utf8_text(command_file)
	return _command_text(getattr(args, "command_text", []))


def _python_code(args: argparse.Namespace) -> str:
	python_file = getattr(args, "python_file", None)
	if python_file:
		return _read_utf8_text(python_file)
	return _command_text(getattr(args, "python_code", []))


def _read_utf8_text(path: str) -> str:
	return _strip_leading_bom(Path(path).read_text(encoding="utf-8-sig"))


def _strip_leading_bom(text: str) -> str:
	return text[1:] if text.startswith("\ufeff") else text


def _request_with_autostart(payload: dict[str, Any], config: ProjectConfig, args: argparse.Namespace) -> dict[str, Any]:
	timeout = _request_timeout(payload)
	try:
		return request_daemon(payload, config=config, timeout=timeout)
	except OSError as exc:
		if getattr(args, "no_daemon", False) or not config.auto_start_daemon:
			return make_error(
				"DAEMON_NOT_RUNNING",
				f"Cannot connect to UE CLI daemon on {HOST}:{config.daemon_port}: {exc}",
				suggested_next="Run `ue daemon start`.",
			)
		started = _start_daemon(config)
		if not started:
			return make_error(
				"DAEMON_START_FAILED",
				f"Could not start UE CLI daemon on {HOST}:{config.daemon_port}",
				suggested_next="Run `ue daemon serve --json` in a terminal for logs.",
			)
		try:
			return request_daemon(payload, config=config, timeout=timeout)
		except OSError as retry_exc:
			return make_error(
				"DAEMON_NOT_RUNNING",
				f"Daemon start attempted, but connection still failed: {retry_exc}",
				suggested_next="Run `ue doctor --json` for diagnostics.",
			)


def _request_timeout(payload: dict[str, Any]) -> float:
	req_type = str(payload.get("type", "")).lower()
	if req_type in {"run", "exec_python"}:
		return 300.0
	return 30.0


def _handle_daemon(args: argparse.Namespace, config: ProjectConfig) -> int:
	cmd = args.daemon_command or "status"
	if cmd == "serve":
		from .daemon import main as daemon_main

		return daemon_main()
	if cmd == "start":
		if getattr(args, "restart", False):
			print(f"OK daemon restart requested on {HOST}:{config.daemon_port}", flush=True)
			_stop_daemon_if_running(config)
		elif can_connect(config.daemon_port):
			print(f"OK daemon already running on {HOST}:{config.daemon_port}")
			print("Use `ue daemon restart` to reload Python modules.")
			return 0
		if _start_daemon(config):
			print(f"OK daemon started on {HOST}:{config.daemon_port}", flush=True)
			return 0
		print(f"ERROR failed to start daemon on {HOST}:{config.daemon_port}", file=sys.stderr)
		return 1
	if cmd == "restart":
		print(f"OK daemon restart requested on {HOST}:{config.daemon_port}", flush=True)
		_stop_daemon_if_running(config)
		if _start_daemon(config):
			print(f"OK daemon restarted on {HOST}:{config.daemon_port}", flush=True)
			return 0
		print(f"ERROR failed to restart daemon on {HOST}:{config.daemon_port}", file=sys.stderr)
		return 1
	if cmd == "stop":
		try:
			response = request_daemon({"type": "stop"}, config=config, timeout=5)
		except OSError as exc:
			print(f"ERROR daemon is not running: {exc}", file=sys.stderr)
			return 1
		print(format_output(response, "text"))
		return 0 if response.get("success") else 1
	if cmd == "status":
		try:
			response = request_daemon({"type": "status"}, config=config, timeout=5)
		except OSError as exc:
			response = make_error(
				"DAEMON_NOT_RUNNING",
				f"Cannot connect to daemon on {HOST}:{config.daemon_port}: {exc}",
				suggested_next="Run `ue daemon start`.",
			)
		return _print_response(response, args)
	print(f"ERROR unknown daemon command: {cmd}", file=sys.stderr)
	return 1


def _stop_daemon_if_running(config: ProjectConfig) -> None:
	if not can_connect(config.daemon_port):
		return
	try:
		request_daemon({"type": "stop"}, config=config, timeout=5)
	except OSError:
		return
	_wait_for_daemon_stop(config)


def _wait_for_daemon_stop(config: ProjectConfig, *, timeout: float = 5.0) -> bool:
	deadline = time.time() + timeout
	while time.time() < deadline:
		if not can_connect(config.daemon_port, timeout=0.2):
			return True
		time.sleep(0.1)
	return False


def _start_daemon(config: ProjectConfig) -> bool:
	if can_connect(config.daemon_port):
		return True

	python_dir = Path(__file__).resolve().parents[1]
	env = os.environ.copy()
	existing = env.get("PYTHONPATH")
	env["PYTHONPATH"] = str(python_dir) if not existing else str(python_dir) + os.pathsep + existing

	creationflags = 0
	if os.name == "nt":
		creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)

	subprocess.Popen(
		[sys.executable, "-m", "ue_cli_tool.cli", "daemon", "serve"],
		cwd=str(python_dir),
		env=env,
		stdin=subprocess.DEVNULL,
		stdout=subprocess.DEVNULL,
		stderr=subprocess.DEVNULL,
		creationflags=creationflags,
		close_fds=True,
	)

	deadline = time.time() + 5.0
	while time.time() < deadline:
		if can_connect(config.daemon_port, timeout=0.2):
			return True
		time.sleep(0.1)
	return False


def _print_response(response: dict[str, Any], args: argparse.Namespace) -> int:
	mode = "json" if getattr(args, "json", False) else "raw" if getattr(args, "raw", False) else "text"
	print(format_output(response, mode))
	return 0 if response.get("success", False) else 1


if __name__ == "__main__":
	raise SystemExit(main())
