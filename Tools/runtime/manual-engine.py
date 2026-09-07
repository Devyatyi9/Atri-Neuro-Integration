"""An OpenAI-compatible server that lets a person -- or an agent -- take the
turns, one at a time, through Gary.

The scripted engine beside this one plays a route without asking anyone. This
one asks. Gary still does all the protocol work, so what reaches the game is a
real `action` command over the real socket, answered with a real result; the
only difference from a run is who decides.

Why this exists next to the MCP harness: the harness talks to the plugin
directly, which is exactly what makes it a poor witness. It cannot show how a
description reads to something that only sees the registry, it does not build a
prompt, and a schema Gary would refuse is one the harness sends anyway. Driving
through Gary exercises the path the game will actually be played over.

Nothing here blocks Gary. A decision is left as a standing order and the next
turn consumes it; a turn that finds no order waits, so an unattended server is
simply a game standing still rather than a client timing out.

Set it up in Gary as an OpenAI-compatible engine with

    Server URL: http://127.0.0.1:5150/v1

and drive it from another shell:

    curl http://127.0.0.1:5150/turn                 what is on offer right now
    curl http://127.0.0.1:5150/context              what the game has been saying
    curl -X POST http://127.0.0.1:5150/act -d "{\\"action\\": \\"advance_text\\"}"
    ... with "unchecked": true to send a name the game never registered
    curl -X POST http://127.0.0.1:5150/wait         pass this turn deliberately
    curl http://127.0.0.1:5150/stop                 end the server
"""

import argparse
import json
import socket
import sys
import threading
import time
from collections import deque
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer

# How many context lines are kept for reading back. A turn adds a line or two,
# and anything older than this is in the game's own logs anyway.
CONTEXT_KEPT = 400

# Actions that change which game this is, or end it. An order for one of these
# is obeyed -- it was asked for on purpose -- but they are never reached for on
# their own when a forced turn has to be answered with something.
NEVER_UNASKED = ("continue_game", "exit_game", "start_true_end", "start_new_game",
                 "load_save", "quick_load", "return_to_title")

# Preferred order for a forced turn nobody left an order for, most harmless
# first: reading a line changes nothing, advancing changes only position.
FORCED_ORDER = ("read_current_line", "advance_text", "close_quick_menu",
                "open_quick_menu")


def stamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def say(text):
    print(f"[{stamp()}] {text}", flush=True)


def offered_actions(schema):
    """The actions this prompt allows, as {name: data schema or None}.

    Returns an empty mapping when the schema is not the shape we expect rather
    than raising: a prompt we cannot read should cost one turn, not the session.
    """
    actions = {}
    try:
        command = schema["properties"]["command"]
    except (KeyError, TypeError):
        return actions
    for branch in command.get("anyOf", [command]):
        # "Perform an action." wraps the real list one level deeper.
        for option in branch.get("anyOf", [branch]):
            if not isinstance(option, dict):
                continue
            properties = option.get("properties", {})
            name = properties.get("action", {}).get("enum", [None])[0]
            if name:
                actions[name] = properties.get("data")
    return actions


def allows_wait(schema):
    try:
        command = schema["properties"]["command"]
    except (KeyError, TypeError):
        return False
    for branch in command.get("anyOf", [command]):
        if isinstance(branch, dict) and "wait" in branch.get("enum", []):
            return True
    return False


def fill(schema):
    """The smallest value a schema accepts, for an action asked for without
    arguments. Gary answers a command with no `data` for an action that declares
    a schema with "Model omitted data" and drops the turn, so something has to
    be there."""
    if not isinstance(schema, dict):
        return None
    if "enum" in schema and schema["enum"]:
        return schema["enum"][0]
    kind = schema.get("type")
    if isinstance(kind, list):
        kind = kind[0] if kind else None
    if kind == "object":
        return {name: fill(schema.get("properties", {}).get(name, {}))
                for name in schema.get("required", [])}
    if kind == "array":
        return []
    if kind in ("integer", "number"):
        return schema.get("minimum", 0)
    if kind == "boolean":
        return False
    return ""


def context_texts(messages):
    """Every context line the prompt carries, newest last.

    Gary does not put the game's messages into the prompt as plain text: each is
    a JSON object on its own line inside a user turn.
    """
    texts = []
    for message in messages:
        content = message.get("content")
        if not isinstance(content, str):
            continue
        for line in content.splitlines():
            line = line.strip()
            if not line:
                continue
            if line.startswith("{"):
                try:
                    entry = json.loads(line)
                except json.JSONDecodeError:
                    texts.append(line)
                    continue
                text = entry.get("text")
                if isinstance(text, str):
                    texts.append(text)
                continue
            texts.append(line)
    return texts


class Engine:
    """What the last turn offered, and what to do on the next one."""

    def __init__(self):
        self.lock = threading.Lock()
        self.turns = 0
        self.actions = {}
        self.wait_allowed = False
        self.context = deque(maxlen=CONTEXT_KEPT)
        self.seen = 0
        # The standing order: what to send on the next turn, or None to wait.
        self.order = None
        # How the previous order ended. Read back through /turn, because an
        # order that named an action nobody was offering would otherwise vanish
        # without a word -- which is the failure this whole tool exists to see.
        self.last = None

    def place(self, action, data, forced=False):
        """Leaves the order for the next turn.

        `forced` skips the check that the action is on offer, for the one thing
        a well-behaved client cannot otherwise do: name something the game never
        registered. The API says to expect that of Neuro, so the game has to
        answer for it, and nothing else here can produce one.
        """
        with self.lock:
            if action is not None and not forced and action not in self.actions:
                return False, (f"{action} is not on offer right now; "
                               f"offered: {', '.join(sorted(self.actions)) or 'nothing'}")
            self.order = (None if action is None
                          else {"action": action, "data": data, "forced": forced})
            if action is None:
                return True, "waiting this turn out"
            return True, f"{action} queued" + (" (unchecked)" if forced else "")

    def observe(self, schema, messages):
        """Takes in the turn Gary is asking about."""
        with self.lock:
            self.turns += 1
            self.actions = offered_actions(schema)
            self.wait_allowed = allows_wait(schema)
            for text in context_texts(messages):
                if not self.context or self.context[-1] != text:
                    self.context.append(text)
                    self.seen += 1

    def decide(self):
        """What to send, and why. Consumes the standing order if there is one."""
        with self.lock:
            order = self.order
            self.order = None

            if order is not None:
                name = order["action"]
                if order["forced"]:
                    command = {"action": name}
                    if order["data"] is not None:
                        command["data"] = order["data"]
                    self.last = f"sent {name} unchecked"
                    return command, "ordered, unchecked"
                if name not in self.actions:
                    # It was offered when it was placed and is not now. Saying so
                    # is the point: silently dropping it looks like the game
                    # ignoring a move.
                    self.last = f"{name} was no longer offered when the turn came"
                    say(f"turn {self.turns}: dropped {name} -- no longer offered")
                    return self._nothing_to_do()
                command = {"action": name}
                data = order["data"]
                if data is not None:
                    command["data"] = data
                elif self.actions[name] is not None:
                    command["data"] = fill(self.actions[name])
                self.last = f"sent {name}"
                return command, "ordered"

            return self._nothing_to_do()

    def _nothing_to_do(self):
        """Called with the lock held."""
        if self.wait_allowed:
            return "wait", "no order standing"
        # A forced turn does not allow waiting, and leaving it unanswered leaves
        # the game holding a force it keeps retrying. Something has to go, but
        # ordered from harmless to game-changing.
        #
        # Readers first, and by name rather than by a list: a `read_` action
        # reports and changes nothing, so answering a force with one leaves the
        # screen exactly as its owner left it. Without this the fallback reached
        # for `go_back` on the settings screen -- a legal answer that closed the
        # screen the next order was written for.
        readers = sorted(name for name in self.actions if name.startswith("read_"))
        if readers:
            return self._forced(readers[0])
        for name in FORCED_ORDER:
            if name in self.actions:
                return self._forced(name)
        neutral = sorted(name for name in self.actions if name not in NEVER_UNASKED)
        if neutral:
            return self._forced(neutral[0])
        if self.actions:
            return self._forced(sorted(self.actions)[0])
        return "wait", "forced with nothing offered"

    def _forced(self, name):
        command = {"action": name}
        if self.actions.get(name) is not None:
            command["data"] = fill(self.actions[name])
        self.last = f"forced turn answered with {name}"
        return command, f"forced, falling back to {name}"

    def snapshot(self):
        with self.lock:
            return {
                "turn": self.turns,
                "actions": {name: schema for name, schema in sorted(self.actions.items())},
                "wait_allowed": self.wait_allowed,
                "order": self.order,
                "last": self.last,
                "context_seen": self.seen,
                "context_tail": list(self.context)[-6:],
            }

    def lines(self, since):
        with self.lock:
            kept = list(self.context)
            first = self.seen - len(kept)
            start = max(0, since - first)
            return {"from": first + start, "seen": self.seen, "lines": kept[start:]}


class Handler(BaseHTTPRequestHandler):
    engine = None
    model = "manual"
    # One small request per turn, so the cost is nearly all connection setup.
    protocol_version = "HTTP/1.1"
    disable_nagle_algorithm = True

    def log_message(self, *_args):
        pass

    def _json(self, status, body):
        payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _body(self):
        length = int(self.headers.get("Content-Length", "0"))
        if not length:
            return {}
        return json.loads(self.rfile.read(length) or b"{}")

    def do_GET(self):
        path = self.path.rstrip("/")
        if path.endswith("/stop"):
            self._json(200, {"stopping": True})
            threading.Thread(target=self.server.shutdown, daemon=True).start()
            return
        if path.endswith("/models"):
            self._json(200, {"object": "list",
                             "data": [{"id": Handler.model, "object": "model"}]})
            return
        if path.endswith("/turn"):
            self._json(200, Handler.engine.snapshot())
            return
        if "/context" in path:
            since = 0
            if "?" in self.path:
                query = self.path.split("?", 1)[1]
                for part in query.split("&"):
                    if part.startswith("since="):
                        try:
                            since = int(part[len("since="):])
                        except ValueError:
                            since = 0
            self._json(200, Handler.engine.lines(since))
            return
        self._json(404, {"error": {"message": f"no route for {self.path}"}})

    def do_POST(self):
        path = self.path.rstrip("/")
        if path.endswith("/act") or path.endswith("/wait"):
            try:
                body = self._body()
            except json.JSONDecodeError as error:
                self._json(400, {"error": {"message": f"unreadable request: {error}"}})
                return
            action = None if path.endswith("/wait") else body.get("action")
            if not path.endswith("/wait") and not action:
                self._json(400, {"error": {"message": "give an \"action\""}})
                return
            placed, why = Handler.engine.place(action, body.get("data"),
                                               bool(body.get("unchecked")))
            say(f"order: {why}")
            self._json(200 if placed else 409, {"placed": placed, "detail": why})
            return

        if not path.endswith("/chat/completions"):
            self._json(404, {"error": {"message": f"no route for {self.path}"}})
            return

        try:
            request = self._body()
        except json.JSONDecodeError as error:
            self._json(400, {"error": {"message": f"unreadable request: {error}"}})
            return

        schema = (request.get("response_format", {})
                  .get("json_schema", {})
                  .get("schema", {}))
        Handler.engine.observe(schema, request.get("messages", []))
        command, why = Handler.engine.decide()
        if why != "no order standing":
            described = command if isinstance(command, str) else command.get("action")
            say(f"turn {Handler.engine.turns}: {described} ({why})")

        self._json(200, {
            "id": f"manual-{Handler.engine.turns}",
            "object": "chat.completion",
            "created": int(time.time()),
            "model": request.get("model", Handler.model),
            "choices": [{
                "index": 0,
                "message": {"role": "assistant",
                            "content": json.dumps({"command": command})},
                "finish_reason": "stop",
            }],
            "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0},
        })


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=5150,
                        help="where Gary reaches this engine (default 5150)")
    parser.add_argument("--model", default="manual")
    options = parser.parse_args()

    # Windows lets a second listener bind a port another process is already on,
    # so starting beside a driver that is still alive would succeed while Gary
    # went on talking to the old process.
    probe = socket.socket()
    probe.settimeout(1.0)
    try:
        if probe.connect_ex(("127.0.0.1", options.port)) == 0:
            say(f"something is already serving on {options.port}; "
                "stop it, or pass --port")
            sys.exit(1)
    finally:
        probe.close()

    Handler.engine = Engine()
    Handler.model = options.model
    server = HTTPServer(("127.0.0.1", options.port), Handler)
    say(f"manual engine on http://127.0.0.1:{options.port}/v1")
    say("in Gary: OpenAI-compatible engine with that Server URL")
    say("drive it with GET /turn, GET /context, POST /act, POST /wait, GET /stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        say("stopped")


if __name__ == "__main__":
    main()
