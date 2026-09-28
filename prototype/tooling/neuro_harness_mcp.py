#!/usr/bin/env python3
"""Local deterministic Neuro protocol harness exposed through MCP.

Frame/Harness logic lives in neuro_harness_core.py, shared with the
standalone assert-based regression script neuro_harness_test.py.
"""

import json
import os
import sys
import tempfile
import threading
import time
from typing import Any

if sys.platform == "win32":
    import msvcrt

    msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
    msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)

from mcp.server.fastmcp import FastMCP

from neuro_harness_core import PORT as DEFAULT_PORT, Harness

# Where a chosen port is remembered between runs. Neither arguments nor the
# environment can carry it: the restart ends this process, and the MCP host
# brings the server back with its own configured command line, so whatever this
# process was told is gone. A file outlives that.
PORT_FILE = os.path.join(tempfile.gettempdir(), "atri-neuro-harness-port")


def _remembered_port() -> int | None:
    try:
        with open(PORT_FILE, encoding="utf-8") as handle:
            return int(handle.read().strip())
    except (OSError, ValueError):
        return None


def _remember_port(port: int | None) -> None:
    if port is None:
        try:
            os.remove(PORT_FILE)
        except OSError:
            pass
        return
    with open(PORT_FILE, "w", encoding="utf-8") as handle:
        handle.write(str(port))


# The characterId sent at startup, kept the same way and for the same reason.
CHARACTER_FILE = os.path.join(tempfile.gettempdir(), "atri-neuro-harness-character")


def _remembered_character() -> str:
    try:
        with open(CHARACTER_FILE, encoding="utf-8") as handle:
            return handle.read().strip() or "test"
    except OSError:
        return "test"


def _remember_character(character: str) -> None:
    if character in ("", "test"):
        try:
            os.remove(CHARACTER_FILE)
        except OSError:
            pass
        return
    with open(CHARACTER_FILE, "w", encoding="utf-8") as handle:
        handle.write(character)


HARNESS = Harness(port=_remembered_port(), character=_remembered_character())
MCP = FastMCP("atri-neuro-harness", log_level="ERROR")


@MCP.tool()
def harness_status() -> str:
    """Return the deterministic local Neuro WebSocket harness status."""
    return json.dumps(HARNESS.status())


@MCP.tool()
def harness_actions() -> str:
    """Return the server-side action registry: which actions are currently
    callable, which were unregistered, and any protocol notes (duplicate
    registers, unregisters of unknown/inactive actions). Use this rather than
    reading raw events to judge whether a capability switch really took effect:
    actions/register is additive, so a stale action stays callable until an
    explicit actions/unregister arrives."""
    return json.dumps(HARNESS.actions())


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
def harness_context(after_sequence: int = 0) -> str:
    """Return only the game's `context` messages (the text Neuro reads), with
    timestamps and the silent flag. Use instead of harness_events when checking
    the context payload, so the output is not buried in protocol traffic."""
    return json.dumps(HARNESS.context_messages(after_sequence))


@MCP.tool()
def harness_force_action(query: str,
                         action_names: list[str],
                         state: str | None = None,
                         ephemeral_context: bool = False,
                         priority: str = "low") -> str:
    """Send actions/force, telling the game it must pick one of action_names now."""
    HARNESS.force_action(query, action_names, state, ephemeral_context, priority)
    return json.dumps({"sent": True, "sequence": HARNESS.status()["last_sequence"]})


@MCP.tool()
def harness_clear_events() -> str:
    """Drop the recorded event log without restarting or dropping the game
    connection. This also clears what harness_context returns, which is a filtered
    view over the same log rather than a separate store. The action registry is
    kept, since it reflects live server state rather than history."""
    HARNESS.clear_events()
    return json.dumps({"cleared": True})


@MCP.tool()
def harness_disconnect_game() -> str:
    """Close the current game WebSocket to test the client's reconnect behavior."""
    HARNESS.disconnect()
    return json.dumps({"disconnected": True})


def _delayed_restart(port: int | None, character: str | None) -> None:
    # Give the MCP transport time to flush this tool's response before the
    # process image is replaced; os.execv never returns on success.
    time.sleep(0.3)
    if port is not None:
        _remember_port(None if port == DEFAULT_PORT else port)
    if character is not None:
        _remember_character(character)
    HARNESS.prepare_restart()
    sys.stdout.flush()
    os.execv(sys.executable, [sys.executable] + sys.argv)


@MCP.tool()
def harness_restart(port: int = 0, character: str = "") -> str:
    """Restart this harness process (re-exec), reloading the script from disk
    and resetting all in-memory state (events, sequence, game connection).

    port: rebind the listener to this port instead of the current one; 0 keeps
    it where it is. Use it to move the harness off 8000 and hand that port to
    another server -- Gary and Randy both hardcode it. The game then has to be
    pointed at whichever one you want:
    `.\Tools
euro_tcp_client.ps1 -Restart -WsUrl "ws://127.0.0.1:<port>"`.

    character: the characterId the startup acknowledgement names from now on
    ("neuro", "evil"; "test" is the default). Empty keeps the current one. The
    game has to reconnect to receive it.

    On Windows, os.execv reuses the calling process's inherited stdio handles
    but does not keep the same PID, so the MCP stdio session stays connected
    without a manual reconnect even though the reported PID changes after this
    call."""
    if port and not (1 <= port <= 65535):
        return json.dumps({"error": f"port out of range: {port}"})
    threading.Thread(target=_delayed_restart,
                     args=(port or None, character or None), daemon=True).start()
    return json.dumps({"restarting": True, "pid": os.getpid(),
                       "port": port or HARNESS.status()["port"]})


if __name__ == "__main__":
    MCP.run(transport="stdio")
