# coding: utf-8
"""Length-prefixed JSON helpers used by the local UE CLI daemon."""

from __future__ import annotations

import json
import socket
from typing import Any

MAX_MESSAGE_BYTES = 100 * 1024 * 1024


def send_json(sock: socket.socket, payload: dict[str, Any]) -> None:
	message = json.dumps(payload, ensure_ascii=False).encode("utf-8")
	length = len(message)
	if length <= 0 or length > MAX_MESSAGE_BYTES:
		raise ValueError(f"Invalid message length: {length}")
	sock.sendall(length.to_bytes(4, byteorder="big"))
	sock.sendall(message)


def recv_json(sock: socket.socket) -> dict[str, Any] | None:
	length_bytes = recv_exact(sock, 4)
	if not length_bytes:
		return None
	length = int.from_bytes(length_bytes, byteorder="big")
	if length <= 0 or length > MAX_MESSAGE_BYTES:
		raise ValueError(f"Invalid message length: {length}")
	message = recv_exact(sock, length)
	if not message:
		return None
	value = json.loads(message.decode("utf-8"))
	if not isinstance(value, dict):
		raise ValueError("IPC payload must be a JSON object")
	return value


def recv_exact(sock: socket.socket, num_bytes: int) -> bytes | None:
	data = bytearray()
	while len(data) < num_bytes:
		chunk = sock.recv(num_bytes - len(data))
		if not chunk:
			return None
		data.extend(chunk)
	return bytes(data)
