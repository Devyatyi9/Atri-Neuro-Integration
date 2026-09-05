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
import re
import socket
import struct
import sys
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer

REPO_TJS_FAST = "Tools/tjs/fast-playback-on.tjs"

# "A choice appeared: 1) Let Atri go. 2) Take care of Atri." -- built by
# DescribeChoiceShown on the plugin side, and the numbering is what
# select_choice takes, so the two cannot drift apart.
CHOICE_HEADER = "A choice appeared:"
CHOICE_OPTION = re.compile(r"(\d+)\)\s*")
CHOICE_REFUSED = "Choice is unavailable"
ADVANCE_REFUSED = "Text advance is unavailable"

# A movie plays past every speed setting the game has, so a run simply stops
# for the length of it -- measured at 98 seconds for the opening and about
# three minutes for the ending. A click ends one, which is what a person does;
# the advance action will not, because it asks whether a click is being waited
# for first, and during a movie nothing is.
#
# Refusals are the only sign from here, and they are not proof: text can be
# refused for a moment for other reasons. So the game is asked directly, and
# only after enough refusals in a row that something is clearly stuck.
REFUSALS_BEFORE_ASKING = 3

# The route's answer for an ordinary turn: nothing of its own to do, hand over
# to the mode. Named so it can be told apart from a route that cannot proceed.
ROUTE_IDLE = "route: nothing to do this turn"
ROUTE_WAITING = "route: waiting for the choice to be taken"

# Neither of the two above is a problem: one is every turn between choices, the
# other is the guard that stops a choice being answered twice while the game
# catches up. Only what is left gets reported.
ROUTE_ROUTINE = (ROUTE_IDLE, ROUTE_WAITING)

# How often the observed pace is reported. The pace is set by the client's own
# poll interval, which lives in its settings and has silently gone back to its
# default once; from here that is invisible except as turns arriving slower, so
# it is worth saying out loud rather than noticing an hour later.
PACE_REPORT_SECONDS = 60


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


def command_for(name, actions, data=None):
    """The command object for an action, with the arguments its schema asks for.

    Built in one place because forgetting them is invisible from here: Gary
    answers a command with no `data` for an action that declares a schema with
    "Model omitted data", drops the turn, and the run simply carries on without
    the move ever reaching the game.
    """
    command = {"action": name}
    schema = actions.get(name)
    if data is not None:
        command["data"] = data
    elif schema is not None:
        command["data"] = fill(schema)
    return command


# Actions that change which run this is, or end it. A driver that has to answer
# a forced turn must never reach for one of these by accident: picking the first
# name alphabetically once resumed a save instead of starting a new game, and
# the whole run afterwards looked healthy while following someone else's story.
NEVER_UNASKED = ("continue_game", "exit_game", "start_true_end", "start_new_game",
                 "load_save", "quick_load", "return_to_title")

# Preferred order for a forced turn, most harmless first: reading a line changes
# nothing, advancing changes only position.
FORCED_ORDER = ("read_current_line", "advance_text", "close_quick_menu",
                "open_quick_menu")


def context_texts(messages):
    """Every context line the prompt carries, newest last.

    Gary does not put the game's messages into the prompt as plain text: each
    one is a JSON object on its own line inside a user turn. Searching the whole
    turn for a phrase therefore finds it, and then reads straight past the end
    of the message into the next object -- which is how a choice option came out
    as `Take care of Atri."}` and matched nothing.
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


def latest_choice(messages):
    """The numbered options of the newest choice in the context, or None.

    Read out of the context rather than tracked here, because the numbering the
    game accepts is the numbering it printed: a choice list built from scenario
    data instead can disagree with what is on screen.
    """
    for text in reversed(context_texts(messages)):
        if CHOICE_HEADER not in text:
            continue
        listing = text.split(CHOICE_HEADER, 1)[1]
        marks = list(CHOICE_OPTION.finditer(listing))
        options = {}
        for position, mark in enumerate(marks):
            end = marks[position + 1].start() if position + 1 < len(marks) else len(listing)
            options[int(mark.group(1))] = listing[mark.end():end].strip()
        return options or None
    return None


def refused_since_choice(messages):
    """Whether the last thing to happen was the game turning a choice down."""
    for text in reversed(context_texts(messages)):
        if CHOICE_REFUSED in text:
            return True
        if CHOICE_HEADER in text:
            return False
    return False


class Engine:
    def __init__(self, options, route=None):
        self.options = options
        self.route = route or {}
        self.turns = 0
        self.decisions = list(self.route.get("choices", []))
        self.taken = 0
        # Indexes already refused for the decision in hand. b402 shows the same
        # text twice, enabled or disabled depending on a flag, so the first
        # match is not always the one the game will accept.
        self.rejected = set()
        self.started = not self.route.get("start_new_game", False)
        self.last_sent = None
        self.pending = False
        self.pending_list = None
        self.auto_armed = False
        self.blocked = None
        self.refusals = 0
        self.leaving = False
        self.pace_since = time.monotonic()
        self.pace_turns = 0

    def choose_for_route(self, actions, schema, messages):
        """The route's own move, or (None, why) to fall through to the mode."""
        if not self.started and "start_new_game" in actions:
            self.started = True
            return (command_for("start_new_game", actions),
                    "route: starting a new game")

        if "select_choice" in actions:
            options = latest_choice(messages)
            if not options:
                return None, "route: the context does not carry the choice list"
            signature = tuple(sorted(options.items()))

            if self.pending and signature != self.pending_list:
                # A different list is up, so the one being worked on is behind
                # us. Tracking the list itself rather than the game's "Chose:"
                # line is what tells the two apart: the answered list can linger
                # for a turn while the registration catches up, and a decision
                # counted then would be spent on a question already answered.
                self.settle_choice()
            elif self.pending:
                if refused_since_choice(messages):
                    if self.last_sent is not None:
                        self.rejected.add(self.last_sent)
                else:
                    return None, ROUTE_WAITING

            number, why = self.route_choice(options)
            if number is None:
                return None, why
            self.last_sent = number
            self.pending = True
            self.pending_list = signature
            return (command_for("select_choice", actions, {"index": number}), why)

        # The list is gone, so the choice this was working on went through. It
        # is counted here rather than at the moment of sending, because sending
        # is not the same as being accepted -- a refused index has to be tried
        # again against the same decision.
        if self.pending:
            self.settle_choice()

        route_done = self.taken >= len(self.decisions)
        # Back at the title with every decision made: the route has run out.
        # Waiting for the title is what makes this work without knowing where
        # a route ends -- the game says so by offering a new game again.
        if route_done and self.route.get("exit_when_done"):
            # Quitting asks for confirmation, and the schema for answering it
            # takes a boolean. Filled from the schema alone that boolean comes
            # out false, which cancels the dialog -- and the run then loops:
            # quit, decline, back to the title, quit again.
            if "answer_dialog" in actions and self.leaving:
                return (command_for("answer_dialog", actions, {"confirm": True}),
                        "route: confirming the quit")
            if "start_new_game" in actions and "exit_game" in actions:
                self.leaving = True
                return (command_for("exit_game", actions),
                        "route: finished, closing the game")

        return None, ROUTE_IDLE

    def route_choice(self, options):
        """The index to send for the decision in hand, if it can be resolved."""
        if self.taken >= len(self.decisions):
            return None, "the route has no decisions left"
        decision = self.decisions[self.taken]

        # By text only, and deliberately without a fallback to a number. A
        # decision that does not match is a route that has drifted from the
        # game -- a changed translation, another language, a typo here -- and
        # picking something by position then sends the run down another branch
        # while everything still looks healthy. A stopped run is cheaper than a
        # wrong one, so this stops and says what it could not find.
        wanted = decision.get("text", "").strip().casefold()
        matches = [number for number, text in sorted(options.items())
                   if text.strip().casefold() == wanted and number not in self.rejected]
        if matches:
            return matches[0], f"route: {decision.get('text')}"
        return None, (f"route: no option reads {decision.get('text')!r}; "
                      f"on offer: {sorted(options.values())}")

    def settle_choice(self):
        """Counts the decision in hand as made and moves to the next one."""
        self.pending = False
        self.pending_list = None
        self.taken += 1
        self.rejected.clear()
        self.last_sent = None

    def arm_auto_mode(self, actions):
        """Turns auto mode on once the story is actually running.

        It cannot be armed at startup with a route: the game is on the title
        then, and auto mode is a property of a running scenario. The story
        announces itself by offering the advance action.
        """
        if self.auto_armed or self.options.mode != "auto":
            return
        if "advance_text" not in actions:
            return
        self.auto_armed = True
        try:
            say("auto mode: " + send_tjs(self.options.tjs_port,
                                         "kag.enterAutoMode(), kag.autoMode").strip())
        except OSError as error:
            say(f"could not turn auto mode on: {error}")

    def unstick_movie(self, messages):
        """Ends a movie the run is waiting on, the way a player would.

        Returns True when a click was sent, so the caller can stop counting.
        """
        try:
            playing = send_tjs(self.options.tjs_port,
                               "kag.isMoviePlaying() ? 1 : 0").strip()
        except OSError as error:
            say(f"could not ask the game about a movie: {error}")
            return False
        if playing != "1":
            return False
        say("a movie is playing and nothing can advance through it; clicking")
        try:
            send_tjs(self.options.tjs_port, "kag.onPrimaryClick(), 1")
        except OSError as error:
            say(f"could not click: {error}")
        return True

    def note_refusals(self, messages):
        """Counts advance refusals since the last turn that got through."""
        for text in reversed(context_texts(messages)):
            if ADVANCE_REFUSED in text:
                self.refusals += 1
                break
            if text.startswith("Text advanced"):
                self.refusals = 0
                break
        if self.refusals >= REFUSALS_BEFORE_ASKING:
            self.refusals = 0
            self.unstick_movie(messages)

    def report_pace(self):
        self.pace_turns += 1
        elapsed = time.monotonic() - self.pace_since
        if elapsed < PACE_REPORT_SECONDS:
            return
        say(f"pace: {self.pace_turns} turns in {elapsed:.0f}s"
            f" ({elapsed * 1000 / self.pace_turns:.0f} ms per turn)")
        self.pace_since = time.monotonic()
        self.pace_turns = 0

    def choose(self, schema, messages=()):
        """The command for this prompt, as the object Gary expects back."""
        self.turns += 1
        self.report_pace()
        actions = offered_actions(schema)
        self.arm_auto_mode(actions)
        if self.options.action in actions:
            self.note_refusals(messages)

        if self.decisions or self.route:
            command, why = self.choose_for_route(actions, schema, messages)
            if command is not None:
                self.blocked = None
                return command, why
            # Said once rather than every turn: when the route cannot resolve a
            # decision it stays stuck on the same one, and the reason repeated
            # five times a second buries everything else in the log. Having
            # nothing to do is not being stuck -- it is every ordinary turn
            # between two choices, and calling it a stall trains the reader to
            # ignore the line that matters.
            if why != self.blocked:
                self.blocked = why
                if why not in ROUTE_ROUTINE:
                    say(f"route stalled: {why}")

        if self.options.mode == "auto" and allows_wait(schema):
            return "wait", "auto mode drives the scene"

        preferred = self.options.action
        if preferred in actions:
            return command_for(preferred, actions), "preferred action"

        if allows_wait(schema):
            return "wait", f"{preferred} not offered"

        # Forced turns do not allow waiting, and refusing one leaves the game
        # holding a force it keeps retrying. Something has to be sent -- but not
        # just anything: the choice is ordered from harmless to run-changing, and
        # the run-changing ones are only reached when nothing else is on offer.
        for name in FORCED_ORDER:
            if name in actions:
                return command_for(name, actions), f"forced, falling back to {name}"
        neutral = sorted(name for name in actions if name not in NEVER_UNASKED)
        if neutral:
            return command_for(neutral[0], actions), f"forced, nothing preferred offered"
        if actions:
            name = sorted(actions)[0]
            return (command_for(name, actions),
                    f"forced with only run-changing actions offered, sending {name}")

        return "wait", "no actions offered"


class Handler(BaseHTTPRequestHandler):
    engine = None
    # The client asks once per turn and the answer is a few hundred bytes, so
    # the cost of a turn is nearly all connection setup. HTTP/1.1 keeps the
    # connection open between turns, and turning off Nagle stops the kernel
    # holding a small response back waiting for more to send.
    protocol_version = "HTTP/1.1"
    disable_nagle_algorithm = True

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
        command, why = Handler.engine.choose(schema, request.get("messages", []))
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
        if options.mode == "auto" and not options.route:
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
    parser.add_argument("--route",
                        help="a JSON route file: the choices to make, in order")
    parser.add_argument("--fast", action="store_true",
                        help="turn the game's speed knobs up; safe here because "
                             "nothing is waiting on a model")
    options = parser.parse_args()

    route = None
    if options.route:
        with open(options.route, encoding="utf-8") as handle:
            route = json.load(handle)
        say(f"route {route.get('name', options.route)}: "
            f"{len(route.get('choices', []))} choice(s)")

    Handler.engine = Engine(options, route)
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
