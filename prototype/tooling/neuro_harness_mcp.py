#!/usr/bin/env python3
"""Local deterministic Neuro protocol harness exposed through MCP."""

import base64
import hashlib
import json
import os
import socket
import struct
import sys
import threading
import time
from collections import deque
from typing import Any

if sys.platform == "win32":
    import msvcrt

    msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
    msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)

from mcp.server.fastmcp import FastMCP


HOST = "127.0.0.1"
PORT = int(os.environ.get("NEURO_HARNESS_PORT", "8000"))
MAX_EVENTS = 256


def _recv_exact(connection: socket.socket, length: int) -> bytes:
    data = bytearray()
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            raise ConnectionError("WebSocket closed")
        data.extend(chunk)
    return bytes(data)


def _read_frame(connection: socket.socket) -> tuple[int, bytes]:
    first, second = _recv_exact(connection, 2)
    opcode = first & 0x0F
    masked = second & 0x80
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", _recv_exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", _recv_exact(connection, 8))[0]
    if length > 1_048_576:
        raise ValueError("WebSocket frame is too large")
    mask = _recv_exact(connection, 4) if masked else b""
    payload = _recv_exact(connection, length)
    if masked:
        payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    return opcode, payload


def _send_frame(connection: socket.socket, opcode: int, payload: bytes = b"") -> None:
    length = len(payload)
    if length < 126:
        header = bytes((0x80 | opcode, length))
    elif length <= 0xFFFF:
        header = bytes((0x80 | opcode, 126)) + struct.pack("!H", length)
    else:
        header = bytes((0x80 | opcode, 127)) + struct.pack("!Q", length)
    connection.sendall(header + payload)


class Harness:
    def __init__(self) -> None:
        self._connection: socket.socket | None = None
        self._events: deque[dict[str, Any]] = deque(maxlen=MAX_EVENTS)
        self._next_sequence = 1
        self._lock = threading.Lock()
        self._event_ready = threading.Condition(self._lock)
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind((HOST, PORT))
        self._listener.listen(1)
        threading.Thread(target=self._accept_loop, daemon=True).start()

    def _record(self, direction: str, message: dict[str, Any]) -> None:
        with self._event_ready:
            self._events.append({
                "sequence": self._next_sequence,
                "direction": direction,
                "message": message,
            })
            self._next_sequence += 1
            self._event_ready.notify_all()

    def _accept_loop(self) -> None:
        while True:
            connection, _ = self._listener.accept()
            threading.Thread(target=self._serve, args=(connection,), daemon=True).start()

    def _serve(self, connection: socket.socket) -> None:
        try:
            request_data = bytearray()
            while b"\r\n\r\n" not in request_data:
                if len(request_data) >= 16 * 1024:
                    raise ValueError("HTTP Upgrade request is too large")
                chunk = connection.recv(4096)
                if not chunk:
                    raise ConnectionError("WebSocket closed during HTTP Upgrade")
                request_data.extend(chunk)
            request = request_data.decode("ascii")
            key = next(
                line.split(":", 1)[1].strip()
                for line in request.split("\r\n")
                if line.lower().startswith("sec-websocket-key:")
            )
            accept = base64.b64encode(
                hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
            ).decode("ascii")
            connection.sendall(
                ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                 "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n").encode("ascii")
            )
            with self._lock:
                previous = self._connection
                self._connection = connection
            self._record("harness", {"event": "connected"})
            if previous:
                previous.close()
            while True:
                opcode, payload = _read_frame(connection)
                if opcode == 8:
                    return
                if opcode == 9:
                    _send_frame(connection, 10, payload)
                    continue
                if opcode != 1:
                    continue
                message = json.loads(payload.decode("utf-8"))
                self._record("client_to_server", message)
                if message.get("command") == "startup":
                    self.send({"command": "startup", "data": {"session": {
                        "sessionId": "neuro-harness", "characterId": "test", "displayName": "Harness"
                    }}})
        except (ConnectionError, OSError, ValueError, json.JSONDecodeError) as error:
            with self._lock:
                server_closed = self._connection is None
            if not server_closed:
                self._record("harness", {"event": "connection_error", "error": str(error)})
        finally:
            with self._lock:
                if self._connection is connection:
                    self._connection = None

    def send(self, message: dict[str, Any]) -> None:
        payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
        with self._lock:
            if not self._connection:
                raise RuntimeError("No game client is connected")
            _send_frame(self._connection, 1, payload)
        self._record("server_to_client", message)

    def status(self) -> dict[str, Any]:
        with self._lock:
            return {"host": HOST, "port": PORT, "connected": self._connection is not None,
                    "last_sequence": self._next_sequence - 1}

    def events(self, after: int) -> list[dict[str, Any]]:
        with self._lock:
            return [event for event in self._events if event["sequence"] > after]

    def wait_for_result(self, action_id: str, after: int, timeout_ms: int) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_ms / 1000
        with self._event_ready:
            while True:
                for event in self._events:
                    message = event["message"]
                    data = message.get("data", {})
                    if (event["sequence"] > after and message.get("command") == "action/result"
                            and data.get("id") == action_id):
                        return event
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"No action/result for {action_id!r}")
                self._event_ready.wait(remaining)

    def disconnect(self) -> None:
        with self._lock:
            if self._connection:
                _send_frame(self._connection, 8)
                self._connection.close()
                self._connection = None

    def prepare_restart(self) -> None:
        """Close the listener and any active game connection ahead of a self re-exec."""
        with self._lock:
            if self._connection:
                try:
                    _send_frame(self._connection, 8)
                except OSError:
                    pass
                self._connection.close()
                self._connection = None
            self._listener.close()


HARNESS = Harness()
MCP = FastMCP("atri-neuro-harness", log_level="ERROR")


@MCP.tool()
def harness_status() -> str:
    """Return the deterministic local Neuro WebSocket harness status."""
    return json.dumps(HARNESS.status())


@MCP.tool()
def harness_events(after_sequence: int = 0) -> str:
    """Return captured client/server protocol messages after a sequence number."""
    return json.dumps(HARNESS.events(after_sequence))


@MCP.tool()
def harness_send_action(action_id: str, name: str, data: Any = None) -> str:
    """Send an action; data is encoded as the protocol's JSON string."""
    message: dict[str, Any] = {"command": "action", "data": {"id": action_id, "name": name}}
    if data is not None:
        message["data"]["data"] = json.dumps(data, separators=(",", ":"))
    HARNESS.send(message)
    return json.dumps({"sent": True, "sequence": HARNESS.status()["last_sequence"]})


@MCP.tool()
def harness_wait_action_result(action_id: str, after_sequence: int = 0, timeout_ms: int = 5000) -> str:
    """Wait for the action/result matching an action ID."""
    return json.dumps(HARNESS.wait_for_result(action_id, after_sequence, timeout_ms))


@MCP.tool()
def harness_disconnect_game() -> str:
    """Close the current game WebSocket to test the client's reconnect behavior."""
    HARNESS.disconnect()
    return json.dumps({"disconnected": True})


def _delayed_restart() -> None:
    # Give the MCP transport time to flush this tool's response before the
    # process image is replaced; os.execv never returns on success.
    time.sleep(0.3)
    HARNESS.prepare_restart()
    sys.stdout.flush()
    os.execv(sys.executable, [sys.executable] + sys.argv)


@MCP.tool()
def harness_restart() -> str:
    """Restart this harness process (re-exec), reloading the script from disk
    and resetting all in-memory state (events, sequence, game connection).
    The listener rebinds to the same port. On Windows, os.execv reuses the
    calling process's inherited stdio handles but does not keep the same
    PID, so the MCP stdio session stays connected without a manual
    reconnect even though the reported PID changes after this call."""
    threading.Thread(target=_delayed_restart, daemon=True).start()
    return json.dumps({"restarting": True, "pid": os.getpid()})


if __name__ == "__main__":
    MCP.run(transport="stdio")
