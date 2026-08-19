#!/usr/bin/env python3
"""Local deterministic Neuro protocol harness exposed through MCP.

Frame/Harness logic lives in neuro_harness_core.py, shared with the
standalone assert-based regression script neuro_harness_test.py.
"""

import json
import os
import sys
import threading
import time
from typing import Any

if sys.platform == "win32":
    import msvcrt

    msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
    msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)

from mcp.server.fastmcp import FastMCP

from neuro_harness_core import Harness

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
