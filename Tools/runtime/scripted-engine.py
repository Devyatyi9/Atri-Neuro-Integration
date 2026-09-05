"""An OpenAI-compatible server that plays the game to a script instead of thinking.

Gary drives a game through whichever engine is selected, and its engine list can
take any OpenAI-compatible server by URL. That is the whole trick here: nothing
in Gary is patched, and the protocol it exercises is the real one -- only the
thing choosing the actions is a rule instead of a model.

Randy is the other option and answers a different question. It picks at random
and retries whatever fails, which is a stress test; a run driven by it is never
the same twice and cannot be compared against yesterday's. This picks the same
action every time, so two runs of one scene differ only where the game differs.
A live model stays useful for asking whether the actions make sense to a reader
-- it just cannot answer whether a build changed the context.

The available actions arrive inside the request: with the "Structured output"
response method, Gary puts them in `response_format.json_schema.schema` as the
`anyOf` of the `command` property. So this reads what it is allowed to do from
the question rather than tracking the session itself.

Two ways to keep the story moving, because they exercise different paths:

  --mode advance  answer every prompt with the advance action, which is what a
                  model does and therefore what the action path really sees
  --mode auto     put the game in auto mode once through the plugin's TCP port
                  and answer "wait" from then on, which leaves the scenario to
                  run at its own pace with the client only watching

Auto mode also allows the speed-up: nothing here is waiting for a model, so the
run is bounded by the scenario rather than by generation.
"""

import argparse
import json
import socket
import struct
import sys
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer

REPO_TJS_FAST = "Tools/tjs/fast-playback-on.tjs"


def stamp():
    # The same shape the plugin writes into the tag log, so a decision here can
    # be lined up against what the game reported without converting anything.
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def say(text):
    print(f"{stamp()} {text}", flush=True)


def send_tjs(port, code):
    """Runs TJS in the game through the plugin's TCP port.

    Length-prefixed UTF-8 both ways, matching Tools/runtime/neuro_tcp_client.ps1.
    """
    with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
        payload = code.encode("utf-8")
        sock.sendall(struct.pack("<I", len(payload)) + payload)
        header = sock.recv(4)
        if len(header) < 4:
            return ""
        (length,) = struct.unpack("<I", header)
        body = b""
        while len(body) < length:
            chunk = sock.recv(length - len(body))
            if not chunk:
                break
            body += chunk
        return body.decode("utf-8", errors="replace")


def offered_actions(schema):
    """The actions this prompt allows, as {name: data schema or None}.

    Returns an empty mapping when the schema is not the shape we expect rather
    than raising: a prompt we cannot read should cost one turn, not the run.
    """
    actions = {}
    try:
        command = schema["properties"]["command"]
    except (KeyError, TypeError):
        return actions
    branches = command.get("anyOf", [command])
    for branch in branches:
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
    """The smallest value a schema accepts.

    A scripted driver has no opinion about action arguments; it needs something
    the game will accept so the action path can be exercised at all.
    """
    if not isinstance(schema, dict):
        return None
    if "enum" in schema and schema["enum"]:
        return schema["enum"][0]
    kind = schema.get("type")
    if isinstance(kind, list):
        kind = kind[0] if kind else None
    if kind == "object":
        result = {}
        for name in schema.get("required", []):
            result[name] = fill(schema.get("properties", {}).get(name, {}))
        return result
    if kind == "array":
        return []
    if kind == "integer" or kind == "number":
        return schema.get("minimum", 0)
    if kind == "boolean":
        return False
    return ""


class Engine:
    def __init__(self, options):
        self.options = options
        self.turns = 0

    def choose(self, schema):
        """The command for this prompt, as the object Gary expects back."""
        self.turns += 1
        actions = offered_actions(schema)

        if self.options.mode == "auto" and allows_wait(schema):
            return "wait", "auto mode drives the scene"

        preferred = self.options.action
        if preferred in actions:
            command = {"action": preferred}
            data_schema = actions[preferred]
            if data_schema is not None:
                command["data"] = fill(data_schema)
            return command, "preferred action"

        if allows_wait(schema):
            return "wait", f"{preferred} not offered"

        # Forced turns do not allow waiting, and refusing one leaves the game
        # holding a force it will keep retrying. Any action beats a stall.
        if actions:
            name = sorted(actions)[0]
            command = {"action": name}
            if actions[name] is not None:
                command["data"] = fill(actions[name])
            return command, "forced, nothing preferred offered"

        return "wait", "no actions offered"


class Handler(BaseHTTPRequestHandler):
    engine = None

    def log_message(self, *_args):
        # The default logger writes to stderr per request; decisions are logged
        # by the handler itself, and the rest is noise during a long run.
        pass

    def _json(self, status, body):
        payload = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        # Some clients probe the model list before their first request.
        if self.path.rstrip("/").endswith("/models"):
            self._json(200, {"object": "list", "data": [
                {"id": Handler.engine.options.model, "object": "model"}
            ]})
            return
        self._json(404, {"error": {"message": f"no route for {self.path}"}})

    def do_POST(self):
        if not self.path.rstrip("/").endswith("/chat/completions"):
            self._json(404, {"error": {"message": f"no route for {self.path}"}})
            return

        length = int(self.headers.get("Content-Length", "0"))
        try:
            request = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError as error:
            self._json(400, {"error": {"message": f"unreadable request: {error}"}})
            return

        schema = (
            request.get("response_format", {})
            .get("json_schema", {})
            .get("schema", {})
        )
        command, why = Handler.engine.choose(schema)
        described = command if isinstance(command, str) else command.get("action")
        say(f"turn {Handler.engine.turns}: {described} ({why})")

        content = json.dumps({"command": command})
        self._json(200, {
            "id": f"scripted-{Handler.engine.turns}",
            "object": "chat.completion",
            "created": int(time.time()),
            "model": request.get("model", Handler.engine.options.model),
            "choices": [{
                "index": 0,
                "message": {"role": "assistant", "content": content},
                "finish_reason": "stop",
            }],
            "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0},
        })


def prepare_game(options):
    """Puts the game in the state the chosen mode needs, once, before serving."""
    if options.mode != "auto" and not options.fast:
        return
    try:
        if options.fast:
            with open(REPO_TJS_FAST, encoding="utf-8") as handle:
                say("fast playback: " + send_tjs(options.tjs_port, handle.read()).strip())
        if options.mode == "auto":
            say("auto mode: " + send_tjs(options.tjs_port, "kag.enterAutoMode(), kag.autoMode").strip())
    except OSError as error:
        say(f"could not reach the game on port {options.tjs_port}: {error}")
        say("start the game first, or pass --tjs-port for the instance you meant")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=5150,
                        help="where this server listens; give Gary http://127.0.0.1:<port>/v1")
    parser.add_argument("--mode", choices=("advance", "auto"), default="advance")
    parser.add_argument("--action", default="advance_text",
                        help="the action to answer with in advance mode")
    parser.add_argument("--model", default="scripted",
                        help="the model id to report; Gary only needs it to be set")
    parser.add_argument("--tjs-port", type=int, default=18701,
                        help="the plugin's TCP port, used by --mode auto and --fast")
    parser.add_argument("--fast", action="store_true",
                        help="turn the game's speed knobs up; safe here because "
                             "nothing is waiting on a model")
    options = parser.parse_args()

    Handler.engine = Engine(options)
    prepare_game(options)

    server = HTTPServer(("127.0.0.1", options.port), Handler)
    say(f"scripted engine on http://127.0.0.1:{options.port}/v1 in {options.mode} mode")
    say("add it in Gary as an OpenAI-compatible engine with that Server URL")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        say("stopped")


if __name__ == "__main__":
    main()
