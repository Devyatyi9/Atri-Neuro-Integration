#!/usr/bin/env python3
"""Standalone assert-based protocol regression test for the ATRI Neuro
integration.

Unlike neuro_harness_mcp.py (an interactive MCP tool for LLM-driven
exploration), this is a scripted, automated pass/fail check with no MCP
client involved -- run it directly:

    py neuro_harness_test.py

It binds its own listener on NEURO_HARNESS_PORT (default 8000), the same
port neuro_harness_mcp.py uses -- only one process can hold it, so stop the
MCP harness first if it is running. With the game already running (its
NeuroClient reconnects on its own), this script waits for that reconnect,
then runs a fixed sequence of protocol-level checks and prints PASS/FAIL
for each. Exits 0 if every check passes, 1 on the first failure or timeout.

Scope: protocol-layer correctness only (registration shape, malformed
payload handling, parameter typing, duplicate-ID suppression, reconnect) --
not game-state navigation (menu transitions, dialogue advance, choices),
which neuro_harness_mcp.py already covers interactively and does not need
scripted assertions for.

The parameter check drives whatever the game has registered at that moment,
so it reads better on a screen with parameterised actions (a save or load
list, the gallery) than on the title screen. It only ever sends payloads
that must be refused; if one were wrongly accepted, the game would act on
it, which is the point of running this against a test install.
"""

import sys
import time
import uuid
from typing import Any, Callable

from neuro_harness_core import Harness

# advance_text takes no parameters and validates its `data` argument before
# checking game state, so it is a safe fixed target for protocol-level
# checks regardless of what the game is currently registered to accept.
PROBE_ACTION = "advance_text"


def wait_for_connection(harness: Harness, timeout_s: float = 30.0) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if harness.status()["connected"]:
            return
        time.sleep(0.2)
    raise AssertionError(f"Game did not connect within {timeout_s}s")


def wait_for_event(
    harness: Harness,
    after: int,
    predicate: Callable[[dict[str, Any]], bool],
    timeout_s: float = 10.0,
    label: str = "",
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        for event in harness.events(after):
            if predicate(event):
                return event
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {label or 'event'}")


def check_startup_and_register(harness: Harness) -> int:
    """Connect sequence: client startup, then a well-formed actions/register."""
    startup_event = wait_for_event(
        harness, 0,
        lambda e: e["direction"] == "client_to_server"
                  and e["message"].get("command") == "startup",
        label="client startup",
    )
    assert startup_event["message"].get("game"), (
        f"startup message missing game name: {startup_event}")

    register_event = wait_for_event(
        harness, startup_event["sequence"],
        lambda e: e["message"].get("command") == "actions/register",
        timeout_s=60.0,
        label="actions/register",
    )
    actions = register_event["message"]["data"]["actions"]
    assert actions, "actions/register sent an empty action list"
    for action in actions:
        assert action.get("name"), f"action missing name: {action}"
        assert "schema" in action, f"action missing schema: {action}"
        assert action["schema"].get("type") == "object", (
            f"schema type is not 'object': {action}")
    return register_event["sequence"]


def check_action_registry_consistent(harness: Harness) -> None:
    """The tracked action set must be sane and free of protocol violations.

    Recording messages alone cannot catch this: `actions/register` is additive,
    so an action the game believes it dropped stays callable on the server until
    an explicit `actions/unregister` arrives. A missing unregister therefore
    looks perfectly fine in the raw event log -- it went unnoticed for a day
    that way -- but shows up here as an action that is still active.
    """
    registry = harness.actions()
    assert registry["active"], (
        f"no action is callable on the server side: {registry}")
    assert not registry["protocol_notes"], (
        f"protocol violations observed: {registry['protocol_notes']}")


def check_register_payload_is_ordered(harness: Harness, after: int) -> int:
    """Each registration payload must be one complete, ordered snapshot."""
    events = harness.events(after)
    register_events = [
        event for event in events
        if event["message"].get("command") == "actions/register"
    ]
    assert register_events, "no actions/register snapshot observed"
    previous_names: list[str] | None = None
    saw_unregister = False
    for event in events:
        command = event["message"].get("command")
        if command == "actions/unregister":
            saw_unregister = True
        if command != "actions/register":
            continue
        actions = event["message"]["data"].get("actions") or []
        names = [action.get("name") for action in actions]
        assert all(names), f"actions/register contains an unnamed action: {actions}"
        assert len(names) == len(set(names)), (
            f"actions/register contains duplicate names: {names}")
        if previous_names is not None and names != previous_names:
            assert saw_unregister, (
                "changed actions/register arrived without a preceding "
                "actions/unregister")
        previous_names = names
        saw_unregister = False
    return register_events[-1]["sequence"]


def check_unknown_action(harness: Harness, after: int) -> int:
    """An unregistered action name must fail gracefully, not hang or crash."""
    action_id = f"regress-unknown-{uuid.uuid4().hex[:8]}"
    harness.send({
        "command": "action",
        "data": {"id": action_id, "name": "totally_bogus_action_name"},
    })
    result = harness.wait_for_result(action_id, after, timeout_ms=5000)
    assert result["message"]["data"]["success"] is False, (
        f"unknown action name unexpectedly succeeded: {result}")
    return result["sequence"]


def check_malformed_action_data(harness: Harness, after: int) -> int:
    """Structurally-invalid `data` must not hang or crash the client."""
    action_id = f"regress-malformed-{uuid.uuid4().hex[:8]}"
    harness.send({
        "command": "action",
        "data": {"id": action_id, "name": PROBE_ACTION, "data": "{not valid json"},
    })
    result = harness.wait_for_result(action_id, after, timeout_ms=5000)
    assert result["message"]["data"]["success"] is False, (
        f"malformed action data unexpectedly succeeded: {result}")
    return result["sequence"]


def _has_typed_parameter(harness: Harness) -> bool:
    return any((schema or {}).get("properties")
               for schema in harness.action_schemas().values())


def _run_action(harness: Harness, name: str, after: int,
                data: str = "{}") -> tuple[int, bool]:
    """Send one well-formed action and report whether it succeeded."""
    action_id = f"regress-nav-{uuid.uuid4().hex[:8]}"
    harness.send({
        "command": "action",
        "data": {"id": action_id, "name": name, "data": data},
    })
    result = harness.wait_for_result(action_id, after, timeout_ms=8000)
    return result["sequence"], bool(result["message"]["data"]["success"])


# Values that are not a valid instance of the declared type, by what the schema
# says the property is. They are written as raw JSON text rather than encoded
# from Python values on purpose: some of the interesting cases -- a bare
# fraction where an integer belongs, two signs, a literal that overflows int --
# are exactly what a JSON encoder would refuse to produce, and they are what a
# hand-rolled reader gets wrong.
BAD_VALUES_BY_TYPE: dict[str, list[str]] = {
    "integer": ["2.9", "1-2", "--3", "99999999999", '"3"', "true", "null"],
    "number": ["1-2", '"3"', "true", "null"],
    "boolean": ['"true"', "1", "truthy", "null"],
    "string": ["7", "true", "null"],
}


def check_typed_parameters_rejected(harness: Harness, after: int) -> tuple[int, int]:
    """A value of the wrong type must be refused, not coerced into a valid one.

    This is the check that the parameter readers cannot be talked into acting on
    something Neuro did not ask for. It matters because the failure it guards
    against is invisible from the outside: before the readers were tightened,
    `{"slot": 2.9}` became slot 2 and `{"page": 1-2}` became page 1 -- values
    that pass every range check the handlers do, so the action succeeded and
    moved the game somewhere real, just not where it was told to.

    The cases come from the schemas the game itself registered, so an action
    that gains a parameter is covered without touching this file, and an action
    that is not on screen right now is simply not among them.

    A refusal for the wrong reason still counts as a pass here -- an action can
    reject a payload because another required property is missing -- so this
    check proves the negative only. What keeps it honest is the registry
    comparison at the end: if any of these payloads had been acted on, the game
    would have moved and the callable set would have changed with it.
    """
    # The suite otherwise takes the game as it finds it, but a screen with no
    # parameterised action leaves this check with nothing to prove. Opening the
    # load list is the cheapest way to get one, and it is put back afterwards.
    opened = False
    if not _has_typed_parameter(harness) and "open_load" in harness.actions()["active"]:
        after, opened = _run_action(harness, "open_load", after)
        time.sleep(1.0)

    try:
        after, tested = _check_typed_parameters(harness, after)
    finally:
        if opened:
            after, _ = _run_action(harness, "go_back", after)
            time.sleep(1.0)
    return after, tested


def _check_typed_parameters(harness: Harness, after: int) -> tuple[int, int]:
    schemas = harness.action_schemas()
    before = harness.actions()["active"]
    tested = 0

    for name in sorted(schemas):
        properties = (schemas[name] or {}).get("properties") or {}
        for prop, definition in sorted(properties.items()):
            declared = (definition or {}).get("type")
            for literal in BAD_VALUES_BY_TYPE.get(declared, []):
                action_id = f"regress-typed-{uuid.uuid4().hex[:8]}"
                harness.send({
                    "command": "action",
                    "data": {
                        "id": action_id,
                        "name": name,
                        "data": f'{{"{prop}": {literal}}}',
                    },
                })
                result = harness.wait_for_result(action_id, after, timeout_ms=8000)
                after = result["sequence"]
                assert result["message"]["data"]["success"] is False, (
                    f"{name} accepted {prop}={literal}, which is not "
                    f"{declared}: {result['message']['data'].get('message')}")
                tested += 1

    assert tested, (
        "no registered action declares a typed parameter right now, so this "
        "check proved nothing -- run it on a screen that has one")

    unchanged = harness.actions()["active"]
    assert unchanged == before, (
        "the callable action set moved while only malformed payloads were "
        f"sent, so one of them was acted on: {before} -> {unchanged}")
    return after, tested


def check_duplicate_id_suppressed(harness: Harness, after: int) -> int:
    """Sending the same action ID twice must produce at most one action/result."""
    action_id = f"regress-dup-{uuid.uuid4().hex[:8]}"
    message = {
        "command": "action",
        "data": {"id": action_id, "name": PROBE_ACTION, "data": "{}"},
    }
    harness.send(message)
    first = harness.wait_for_result(action_id, after, timeout_ms=5000)
    harness.send(message)  # exact duplicate ID
    time.sleep(1.0)  # give the client a chance to (incorrectly) respond again
    duplicates = [
        event for event in harness.events(first["sequence"])
        if event["message"].get("command") == "action/result"
        and event["message"]["data"].get("id") == action_id
    ]
    assert not duplicates, f"duplicate action ID produced a second result: {duplicates}"
    return first["sequence"]


def check_unregister_names_the_previous_set(harness: Harness) -> None:
    """A changed set must be preceded by an unregister naming what it replaces.

    check_register_payload_is_ordered only asks whether some unregister was
    seen. That passes even when the unregister lists the wrong names, which
    leaves the dropped actions callable on the server -- the exact failure the
    registry check catches after the fact, caught here at the message level.
    """
    events = harness.events(0)
    active: list[str] = []
    checked = 0
    for event in events:
        message = event["message"]
        command = message.get("command")
        if command == "actions/unregister":
            names = message["data"].get("action_names") or []
            assert set(names) == set(active), (
                f"unregister names {sorted(names)} but the active set is "
                f"{sorted(active)}")
            active = []
            checked += 1
        elif command == "actions/register":
            # Re-registering an active name is legitimate -- it replaces the
            # definition -- so only the unregister side is asserted.
            active = [a.get("name") for a in message["data"].get("actions") or []]
    assert checked, "no actions/unregister observed at all"


def check_rapid_capability_switches(harness: Harness) -> int:
    """Screens switched back to back must still leave a consistent set.

    Each transition is a full unregister/register pair, so switching faster
    than the refresh settles is where a stale action would survive.
    """
    registry = harness.actions()
    if "open_extra" not in registry["active"]:
        print("SKIP: rapid switches need the title screen", flush=True)
        return harness.status()["last_sequence"]

    # Notes accumulate for the whole run, and an earlier check provokes one on
    # purpose, so only the ones added here count.
    notes_before = len(registry["protocol_notes"])
    after = harness.status()["last_sequence"]
    for round_index in range(3):
        for name in ("open_extra", "go_back"):
            action_id = f"regress-switch-{round_index}-{name}-{uuid.uuid4().hex[:6]}"
            harness.send({
                "command": "action",
                "data": {"id": action_id, "name": name, "data": "{}"},
            })
            result = harness.wait_for_result(action_id, after, timeout_ms=8000)
            assert result["message"]["data"]["success"] is True, (
                f"{name} failed mid-switch: {result}")
            after = result["sequence"]

    # The set has to settle back to the screen we started from.
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if "open_extra" in harness.actions()["active"]:
            break
        time.sleep(0.2)
    final = harness.actions()
    assert "open_extra" in final["active"], (
        f"the title set did not come back after switching: {final['active']}")
    assert "view_cg" not in final["active"], (
        f"an Extra action survived the return to the title: {final['active']}")
    new_notes = final["protocol_notes"][notes_before:]
    assert not new_notes, f"switching produced protocol violations: {new_notes}"
    return after


def check_capability_switch_latency(harness: Harness) -> int:
    """How long a screen change takes to reach Neuro as a new action set.

    The action's own result says the button was pressed; what matters for
    playing is when the set that describes the new screen arrives, which is a
    separate message and can trail the result while the scene loads.
    """
    registry = harness.actions()
    if "open_extra" not in registry["active"]:
        print("SKIP: switch latency needs the title screen", flush=True)
        return harness.status()["last_sequence"]

    # Each step is the action to send and a name that only the arriving set has,
    # so the register being waited for is unmistakably the new screen's.
    steps = [("open_extra", "view_cg"), ("go_back", "open_extra")]
    after = harness.status()["last_sequence"]
    for action_name, marker in steps * 2:
        action_id = f"regress-swlat-{uuid.uuid4().hex[:8]}"
        started = time.monotonic()
        harness.send({
            "command": "action",
            "data": {"id": action_id, "name": action_name, "data": "{}"},
        })
        result = harness.wait_for_result(action_id, after, timeout_ms=8000)
        assert result["message"]["data"]["success"] is True, (
            f"{action_name} failed: {result}")
        result_at = time.monotonic() - started

        event = wait_for_event(
            harness, after,
            lambda e, m=marker: e["message"].get("command") == "actions/register"
            and m in [a.get("name") for a in e["message"]["data"].get("actions") or []],
            timeout_s=10.0,
            label=f"actions/register carrying {marker}",
        )
        register_at = time.monotonic() - started
        after = max(event["sequence"], result["sequence"])
        print(f"       {action_name}: result {result_at * 1000:.0f}ms, "
              f"set {register_at * 1000:.0f}ms", flush=True)
        assert register_at < 5.0, (
            f"the set for {marker} took {register_at:.1f}s to arrive")
    return after


def check_action_result_latency(harness: Harness, after: int) -> int:
    """Every action must be answered, and quickly enough to stay usable.

    The core already flags a result that arrives past the server's own limit;
    this measures the ordinary case, so a regression shows as a number rather
    than as the game feeling slow.
    """
    samples: list[float] = []
    for _ in range(8):
        action_id = f"regress-latency-{uuid.uuid4().hex[:8]}"
        started = time.monotonic()
        harness.send({
            "command": "action",
            "data": {"id": action_id, "name": PROBE_ACTION, "data": "{}"},
        })
        result = harness.wait_for_result(action_id, after, timeout_ms=5000)
        samples.append(time.monotonic() - started)
        after = result["sequence"]
    worst = max(samples)
    median = sorted(samples)[len(samples) // 2]
    assert worst < 2.0, (
        f"slowest action/result took {worst:.2f}s, median {median:.2f}s")
    print(f"       latency: median {median * 1000:.0f}ms, worst "
          f"{worst * 1000:.0f}ms over {len(samples)} calls", flush=True)
    return after


def check_burst_is_fully_answered(harness: Harness, after: int) -> int:
    """A burst sent without waiting must be answered once per action.

    Kept under the SDK's inbound queue, which holds MESSAGE_QUEUE_SIZE = 10
    messages: the eleventh unpolled message sets NeuroSDK_MessageQueueFull and
    the connection is dropped, so a larger burst would test the disconnect
    rather than the draining. Polling is every 100ms, which is the window that
    ceiling applies over.
    """
    action_ids = [f"regress-burst-{index}-{uuid.uuid4().hex[:6]}" for index in range(8)]
    for action_id in action_ids:
        harness.send({
            "command": "action",
            "data": {"id": action_id, "name": PROBE_ACTION, "data": "{}"},
        })

    deadline = time.monotonic() + 20.0
    answered: dict[str, int] = {}
    # Each pass re-reads every event after the same point, so a result already
    # counted would be counted again; the sequence numbers keep them apart.
    seen: set[int] = set()
    while time.monotonic() < deadline and len(answered) < len(action_ids):
        for event in harness.events(after):
            if event["sequence"] in seen:
                continue
            seen.add(event["sequence"])
            message = event["message"]
            if message.get("command") != "action/result":
                continue
            result_id = message["data"].get("id")
            if result_id in action_ids:
                answered[result_id] = answered.get(result_id, 0) + 1
        if len(answered) < len(action_ids):
            time.sleep(0.2)

    missing = [a for a in action_ids if a not in answered]
    assert not missing, (
        f"{len(missing)} of {len(action_ids)} burst actions went unanswered")
    repeated = {a: n for a, n in answered.items() if n > 1}
    assert not repeated, f"burst actions answered more than once: {repeated}"
    assert harness.status()["connected"], "the game dropped the connection under a burst"
    return harness.status()["last_sequence"]


def check_reconnect(harness: Harness) -> None:
    """Disconnecting the game must not stop it from reconnecting on its own."""
    before = harness.status()["last_sequence"]
    harness.disconnect()
    wait_for_connection(harness, timeout_s=15.0)
    wait_for_event(
        harness, before,
        lambda e: e["direction"] == "client_to_server"
                  and e["message"].get("command") == "startup",
        timeout_s=15.0,
        label="startup after reconnect",
    )


def main() -> int:
    print(f"Binding harness listener (this claims the port neuro_harness_mcp.py "
          f"also uses -- stop it first if it's running)...", flush=True)
    harness = Harness()

    try:
        print("Waiting for the game to connect...", flush=True)
        wait_for_connection(harness)
        print("PASS: game connected", flush=True)

        after = check_startup_and_register(harness)
        print("PASS: startup + well-formed actions/register", flush=True)

        check_action_registry_consistent(harness)
        print("PASS: server-side action registry consistent", flush=True)

        after = check_register_payload_is_ordered(harness, 0)
        print("PASS: actions/register is a complete ordered snapshot", flush=True)

        after = check_unknown_action(harness, after)
        print("PASS: unknown action name rejected", flush=True)

        after = check_malformed_action_data(harness, after)
        print("PASS: malformed action data rejected (no hang/crash)", flush=True)

        after, typed_cases = check_typed_parameters_rejected(harness, after)
        print(f"PASS: {typed_cases} wrongly-typed parameters refused, not coerced",
              flush=True)

        after = check_duplicate_id_suppressed(harness, after)
        print("PASS: duplicate action ID produced no second result", flush=True)

        check_unregister_names_the_previous_set(harness)
        print("PASS: every unregister named the set it replaced", flush=True)

        after = check_rapid_capability_switches(harness)
        print("PASS: rapid screen switches left no stale actions", flush=True)

        after = check_action_result_latency(harness, after)
        print("PASS: action/result latency within budget", flush=True)

        after = check_capability_switch_latency(harness)
        print("PASS: the new action set follows a screen change", flush=True)

        after = check_burst_is_fully_answered(harness, after)
        print("PASS: burst of 8 actions answered exactly once each", flush=True)

        check_reconnect(harness)
        print("PASS: game reconnected automatically after disconnect", flush=True)

    except AssertionError as error:
        print(f"FAIL: {error}", flush=True)
        return 1
    except TimeoutError as error:
        print(f"FAIL (timeout): {error}", flush=True)
        return 1

    print("All regression checks passed.", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
