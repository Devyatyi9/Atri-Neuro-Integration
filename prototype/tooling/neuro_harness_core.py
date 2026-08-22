"""Shared WebSocket loopback harness for the ATRI Neuro protocol.

Extracted from neuro_harness_mcp.py so the same Harness/frame logic can back
both the interactive MCP tool (neuro_harness_mcp.py) and the standalone
assert-based regression script (neuro_harness_test.py) without duplication.
Importing this module has no side effects -- unlike the MCP entry point, it
does not create a Harness() or bind a socket at import time.
"""

import base64
import hashlib
import json
import os
import socket
import struct
import threading
import time
from collections import deque
from typing import Any

HOST = "127.0.0.1"
PORT = int(os.environ.get("NEURO_HARNESS_PORT", "8000"))
ACTION_RESULT_LIMIT_S = 20.0
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
    def __init__(self, port: int | None = None) -> None:
        # The port is per-instance so a caller can put the harness somewhere
        # else and leave the default free for another server -- Gary and Randy
        # both want 8000 and neither offers a way to move.
        self._port = PORT if port is None else port
        self._connection: socket.socket | None = None
        self._events: deque[dict[str, Any]] = deque(maxlen=MAX_EVENTS)
        self._next_sequence = 1
        self._lock = threading.Lock()
        self._event_ready = threading.Condition(self._lock)
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind((HOST, self._port))
        self._listener.listen(1)
        # Server-side action registry, mirroring what a real server maintains.
        # Recording raw messages is not enough to catch protocol-state bugs: a
        # missing actions/unregister looked perfectly fine in the event log for a
        # full day while the action stayed live on the real server.
        self._actions: dict[str, dict[str, Any]] = {}
        self._protocol_notes: deque[str] = deque(maxlen=MAX_EVENTS)
        # Action id -> when it was sent, so a late result can be named. The real
        # server gives up after about 20 seconds, treats the action as failed and
        # discards a result that arrives afterwards, so a game that answers
        # slower than that is silently broken rather than merely slow.
        self._pending_actions: dict[str, float] = {}
        # Names of an outstanding actions/force, or None when none is pending.
        # The force is satisfied once one of its actions is dispatched.
        self._forced_names: list[str] | None = None
        threading.Thread(target=self._accept_loop, daemon=True).start()

    def _apply_action_timing(self, message: dict[str, Any]) -> None:
        """Flag results the real server would already have given up on."""
        data = message.get("data") or {}
        action_id = data.get("id")
        if not action_id:
            return
        sent_at = self._pending_actions.pop(action_id, None)
        if sent_at is None:
            self._protocol_notes.append(
                f"result for an action nobody sent: {action_id}")
            return
        elapsed = time.time() - sent_at
        if elapsed > ACTION_RESULT_LIMIT_S:
            self._protocol_notes.append(
                f"action/result after {elapsed:.1f}s, past the server's "
                f"~{ACTION_RESULT_LIMIT_S:.0f}s limit: {action_id}")

    def _apply_action_registry(self, message: dict[str, Any]) -> None:
        """Track the action set the way the reference server does.

        actions/register is additive and never implies removal; only
        actions/unregister deactivates. Re-registering a name that is already
        active is legitimate and replaces its definition -- the specification
        changed on 2026-08-22, from "it will be ignored" to "the new definition
        replaces the old one" -- so that is not flagged; a re-register that
        changes nothing is, because it is traffic for nothing.
        """
        command = message.get("command")
        data = message.get("data") or {}
        if command == "actions/register":
            for action in data.get("actions") or []:
                name = action.get("name")
                if not name:
                    continue
                existing = self._actions.get(name)
                if (existing and existing["active"]
                        and existing["action"] == action):
                    self._protocol_notes.append(
                        f"re-register with no change: {name}")
                self._actions[name] = {"action": action, "active": True}
        elif command == "actions/unregister":
            for name in data.get("action_names") or []:
                existing = self._actions.get(name)
                if existing is None:
                    self._protocol_notes.append(
                        f"unregister of never-registered action: {name}")
                elif not existing["active"]:
                    self._protocol_notes.append(
                        f"unregister of already-inactive action: {name}")
                else:
                    existing["active"] = False
        elif command == "actions/force":
            # The server handles one thing at a time: it refuses a force while
            # an action result is still owed, and it handles one force at a
            # time. Both were caught by Gary before this harness could see
            # them, which is the gap these notes close.
            if self._pending_actions:
                owed = ", ".join(sorted(self._pending_actions))
                self._protocol_notes.append(
                    f"actions/force while a result is still owed: {owed}")
            if self._forced_names is not None:
                self._protocol_notes.append(
                    "actions/force while an earlier force is unanswered: "
                    f"{', '.join(self._forced_names)}")
            self._forced_names = list(data.get("action_names") or [])
            for name in self._forced_names:
                entry = self._actions.get(name)
                if entry is None or not entry["active"]:
                    self._protocol_notes.append(
                        f"actions/force names an action that is not "
                        f"registered: {name}")

    def actions(self) -> dict[str, Any]:
        """Current server-side view: which actions the game could actually call."""
        with self._lock:
            return {
                "active": sorted(name for name, entry in self._actions.items()
                                 if entry["active"]),
                "inactive": sorted(name for name, entry in self._actions.items()
                                   if not entry["active"]),
                "protocol_notes": list(self._protocol_notes),
            }

    def _record(self, direction: str, message: dict[str, Any]) -> None:
        with self._event_ready:
            self._events.append({
                "sequence": self._next_sequence,
                # Epoch milliseconds, so latency between a game-side change and
                # the resulting protocol message can be measured from the log
                # instead of guessed with a stopwatch.
                "timestamp_ms": int(time.time() * 1000),
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
                # A reconnect is a fresh game session on the server side, so the
                # action registry starts empty again.
                self._actions.clear()
                self._protocol_notes.clear()
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
                with self._lock:
                    self._apply_action_registry(message)
                    if message.get("command") == "action/result":
                        self._apply_action_timing(message)
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
                    # Drop the action registry on disconnect, matching the
                    # reference server (gary removes the game entirely and
                    # emits api/registry/game_removed). Keeping it would report
                    # actions as callable while nothing is connected.
                    self._actions.clear()
                    self._protocol_notes.clear()
                    # A force and an owed result belong to the connection that
                    # carried them; gary clears its force queue and its
                    # pending actions in onclose for the same reason. Keeping
                    # them would blame the next session's first force for a
                    # dialog the previous one left open.
                    self._pending_actions.clear()
                    self._forced_names = None

    def send(self, message: dict[str, Any]) -> None:
        payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
        with self._lock:
            if not self._connection:
                raise RuntimeError("No game client is connected")
            if message.get("command") == "action":
                action_id = (message.get("data") or {}).get("id")
                if action_id:
                    self._pending_actions[action_id] = time.time()
                name = (message.get("data") or {}).get("name")
                if self._forced_names is not None and name in self._forced_names:
                    self._forced_names = None
            _send_frame(self._connection, 1, payload)
        self._record("server_to_client", message)

    def status(self) -> dict[str, Any]:
        with self._lock:
            return {"host": HOST, "port": self._port, "connected": self._connection is not None,
                    "last_sequence": self._next_sequence - 1}

    def events(self, after: int) -> list[dict[str, Any]]:
        with self._lock:
            return [event for event in self._events if event["sequence"] > after]

    def context_messages(self, after: int = 0) -> list[dict[str, Any]]:
        """Only the game's `context` messages -- the payload Neuro actually reads.

        Separate from events() so inspecting the context stream does not mean
        paging through every register/result message alongside it.
        """
        with self._lock:
            return [
                {"sequence": event["sequence"],
                 "timestamp_ms": event["timestamp_ms"],
                 "silent": (event["message"].get("data") or {}).get("silent"),
                 "message": (event["message"].get("data") or {}).get("message")}
                for event in self._events
                if event["sequence"] > after
                and event["direction"] == "client_to_server"
                and event["message"].get("command") == "context"
            ]

    def force_action(self, query: str, action_names: list[str],
                     state: str | None = None, ephemeral_context: bool = False,
                     priority: str = "low") -> None:
        """Send actions/force, the server's 'you must act now' request."""
        data: dict[str, Any] = {
            "query": query,
            "action_names": action_names,
            "ephemeral_context": ephemeral_context,
            "priority": priority,
        }
        if state is not None:
            data["state"] = state
        self.send({"command": "actions/force", "data": data})

    def clear_events(self) -> None:
        """Drop recorded events without restarting; keeps the connection and the
        action registry, so a test can start from a clean log mid-session.

        context_messages() is a filtered view over this same log, so it is
        cleared by this call too -- there is no separate context store.
        """
        with self._lock:
            self._events.clear()

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
