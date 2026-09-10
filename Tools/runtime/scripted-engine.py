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

Several routes can be given at once and are played back to back, each starting
from the title the previous one came back to -- which is also how the end of a
route is recognised, so a chain needs no knowledge of where any of them ends.
That is what makes one run cover as much of the game as the routes name:

  --route routes/route-a.json
  --route routes/route-a.json routes/route-b.json
  --route routes/route-a.json routes/route-b.json routes/true-end.json

Only the last route may close the game; an exit_when_done in the middle would
end the run with routes still unplayed.

To end a run early, press Ctrl+C or fetch /stop on this server's own port. Both
finish through the same path and put the player's speed settings back; killing
the process does not, and then they have to be restored by hand.
"""

import argparse
import ctypes
import json
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer

REPO_TJS_FAST = "Tools/tjs/fast-playback-on.tjs"
REPO_TJS_SLOW = "Tools/tjs/fast-playback-off.tjs"

# "A choice appeared: 1) Let Atri go. 2) Take care of Atri." -- built by
# DescribeChoiceShown on the plugin side, and the numbering is what
# select_choice takes, so the two cannot drift apart.
CHOICE_HEADER = "A choice appeared:"
CHOICE_OPTION = re.compile(r"(\d+)\)\s*")
CHOICE_REFUSED = "Choice is unavailable"
# Both ways the game turns an advance down. The second is the movie's own
# wording, and it has to be listed here rather than only read: counting refusals
# is what eventually clicks through a movie, and a refusal that does not match
# leaves the run watching the whole thing. That is exactly what happened when
# the movie got a message of its own.
ADVANCE_REFUSED = ("Text advance is unavailable", "A movie is playing")

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

# How often to record the game's memory when nothing better marks the run. This
# is the fallback: with --watch-tag-log the samples go on scene changes instead,
# which is where the evidence is. Without it, a slope every so often is all
# there is.
MEMORY_EVERY_TURNS = 500

# How often to check the game is still there. Cheap, but not free, and a run
# that ended does not need noticing within the same second.
GAME_CHECK_EVERY_TURNS = 50

# Lines the engine writes when it is in trouble but still running. The first one
# ran for three minutes and several hundred lines before the game gave up.
TROUBLE_IN_THE_GAME_LOG = (
    "could not allocate sufficient memory",
    "An exception occured at system.tjs",
)

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


def game_pid(port):
    """The process holding the plugin's TCP port, which is the game itself.

    Asked for by port rather than by executable name: two copies of the game run
    from one folder and carry nearly the same name, so matching by name picks
    whichever answers first -- and then every memory reading afterwards belongs
    to the wrong instance while looking perfectly reasonable.
    """
    try:
        listing = subprocess.run(["netstat", "-ano", "-p", "TCP"],
                                 capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError) as error:
        say(f"could not list sockets to find the game: {error}")
        return None
    for line in listing.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[3] == "LISTENING" and parts[1].endswith(f":{port}"):
            return int(parts[4])
    return None


class MemoryCounters(ctypes.Structure):
    # PROCESS_MEMORY_COUNTERS_EX: the private bytes at the end are what matters
    # for a 32-bit process, since that is what the address space runs out of.
    _fields_ = [("cb", ctypes.c_ulong),
                ("PageFaultCount", ctypes.c_ulong),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
                ("PrivateUsage", ctypes.c_size_t)]


def process_is_alive(pid):
    """Whether that process still exists."""
    STILL_ACTIVE = 259
    handle = ctypes.windll.kernel32.OpenProcess(0x0400, False, pid)
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        if not ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
            return True
        return code.value == STILL_ACTIVE
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


def process_memory(pid):
    """Working set and private bytes for one process, in megabytes."""
    PROCESS_QUERY_INFORMATION = 0x0400
    PROCESS_VM_READ = 0x0010
    handle = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not handle:
        return None
    try:
        counters = MemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        if not ctypes.windll.psapi.GetProcessMemoryInfo(
                handle, ctypes.byref(counters), counters.cb):
            return None
        return (counters.WorkingSetSize / (1024 * 1024),
                counters.PrivateUsage / (1024 * 1024))
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


# Counting the layers the game is holding, asked of the engine.
#
# Bytes alone cannot tell "more objects" from "the same objects holding more",
# and that is the fork the crash left open. This counts what is reachable from
# the primary layer, which is what a leak inside the tree would show up in.
#
# What it cannot show: a layer that fell out of the tree and was never freed.
# For that the engine's own lists would have to be read, and those are C++
# internals with no way in from here.
#
# The walker lives in `global` because a local function in TJS cannot find
# itself by name to recurse.
LAYER_COUNT_TJS = (
    "(function(){ global.__mem_n = 0;"
    " global.__mem_walk = function(l) { global.__mem_n++;"
    " var c = l.children;"
    " for (var i = 0; i < c.count; i++) global.__mem_walk(c[i]); };"
    " global.__mem_walk(kag.primaryLayer);"
    " return global.__mem_n; })()")


def layer_count(port):
    """How many layers hang off the primary layer, or None if it cannot be asked."""
    try:
        answer = send_tjs(port, LAYER_COUNT_TJS).strip()
    except OSError:
        return None
    try:
        return int(answer)
    except ValueError:
        return None


def file_version(path):
    """The version stamped into a Windows binary, or None.

    Worth recording next to the build stamp, because the two answer different
    questions: this says which upstream release it is, the stamp says which of
    our builds of it. Ours all report the same version -- it is a number the
    author sets by hand in the manifest, not something a rebuild moves.
    """
    size = ctypes.windll.version.GetFileVersionInfoSizeW(path, None)
    if not size:
        return None
    buffer = ctypes.create_string_buffer(size)
    if not ctypes.windll.version.GetFileVersionInfoW(path, 0, size, buffer):
        return None
    block = ctypes.c_void_p()
    length = ctypes.c_uint()
    if not ctypes.windll.version.VerQueryValueW(
            buffer, "\\", ctypes.byref(block), ctypes.byref(length)):
        return None
    # VS_FIXEDFILEINFO: the two version words sit at offsets 8 and 12, most
    # significant half first.
    fixed = (ctypes.c_uint * 4).from_address(block.value)
    if fixed[0] != 0xFEEF04BD:
        return None
    most, least = fixed[2], fixed[3]
    return (f"{most >> 16}.{most & 0xFFFF}."
            f"{least >> 16}.{least & 0xFFFF}")


def build_stamp(path):
    """Size and mtime of a file, as one string.

    Not its version number: every build of Gary we make calls itself 1.0.1, and
    the plugin has reported 100 for months. What actually tells two builds apart
    is when the file was written.
    """
    try:
        info = os.stat(path)
    except OSError:
        return None
    when = datetime.fromtimestamp(info.st_mtime).strftime("%Y-%m-%d %H:%M")
    return f"{info.st_size} bytes, built {when}"


def game_directory(port):
    """Where the game is installed, asked of the game itself.

    System.exePath comes back as a file:// URL with forward slashes, which is
    not a path anything else here can open.
    """
    try:
        raw = send_tjs(port, "System.exePath").strip()
    except OSError:
        return None
    if not raw.startswith("file://"):
        return None
    rest = raw[len("file://"):].lstrip("./")
    parts = rest.split("/", 1)
    if not parts or not parts[0]:
        return None
    tail = parts[1] if len(parts) > 1 else ""
    return os.path.join(parts[0].upper() + ":\\", tail.replace("/", "\\"))


class MemoryRegion(ctypes.Structure):
    """MEMORY_BASIC_INFORMATION as a 64-bit caller sees it.

    The game is 32-bit, but this process is not, and the fields are laid out for
    the caller. Walking stops at the end of the 32-bit user range, which is all
    the game can ever use.
    """
    _fields_ = [("BaseAddress", ctypes.c_ulonglong),
                ("AllocationBase", ctypes.c_ulonglong),
                ("AllocationProtect", ctypes.c_ulong),
                ("__alignment1", ctypes.c_ulong),
                ("RegionSize", ctypes.c_ulonglong),
                ("State", ctypes.c_ulong),
                ("Protect", ctypes.c_ulong),
                ("Type", ctypes.c_ulong),
                ("__alignment2", ctypes.c_ulong)]


def largest_free_block(pid):
    """The biggest run of free address space in that process, in megabytes.

    This is the axis the crash was on: the engine asked for 33MB in one piece
    and did not get it, while the process was nowhere near out of memory in
    total. Total bytes cannot show that, and neither can private bytes.
    """
    PROCESS_QUERY_INFORMATION = 0x0400
    MEM_FREE = 0x10000
    # The end of the user-mode range a 32-bit process gets with LARGEADDRESSAWARE.
    limit = 0xFFFF0000
    handle = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        region = MemoryRegion()
        address = 0
        biggest = 0
        while address < limit:
            written = ctypes.windll.kernel32.VirtualQueryEx(
                handle, ctypes.c_void_p(address), ctypes.byref(region),
                ctypes.sizeof(region))
            if not written:
                break
            if region.State == MEM_FREE and region.RegionSize > biggest:
                biggest = region.RegionSize
            step = region.RegionSize or 0x1000
            address = region.BaseAddress + step
        return biggest / (1024 * 1024)
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


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
    def __init__(self, options, routes=None):
        self.options = options
        # Routes are played one after another in the order given. Between them
        # the game passes through the title, which is also how the end of a
        # route is recognised, so chaining needs no knowledge of where any of
        # them ends -- the game says so by offering a new game again.
        self.routes = list(routes or [])
        self.route_index = 0
        self.turns = 0
        # Only the last route may close the game: an exit_when_done in the
        # middle of a chain would end the run with routes still unplayed.
        self.finish = options.exit_when_done or bool(
            (self.routes[-1] if self.routes else {}).get("exit_when_done"))
        self.load_route()
        self.last_sent = None
        self.pending = False
        self.pending_list = None
        # The last choice list seen in the context. The client trims the prompt
        # to its context window, so a list can scroll out of view between the
        # turn it appears on and the turn the answer is sent -- at a small
        # window that happened within seconds, and the route simply stopped.
        #
        # Held only until the choice it belongs to is done. Carrying it further
        # would be worse than stalling: the next choice could be answered with
        # the previous one's options, and the run would take a branch nobody
        # chose while still looking healthy. Reset per route by load_route for
        # the same reason, across the title between two of them.
        self.auto_armed = False
        self.blocked = None
        self.refusals = 0
        self.leaving = False
        self.speed_restored = False
        self.pace_since = time.monotonic()
        self.pace_turns = 0
        self.game_pid = game_pid(options.tjs_port)
        self.stopping = False
        # Set once the server exists, so the run can end itself.
        self.on_stop = None
        self.watch_log = options.watch_game_log
        self.tag_log = options.watch_tag_log
        self.tag_read_to = 0
        self.memory_limit = options.memory_limit
        self.dump_with = options.dump_with
        # Only what the run itself produces is read: whatever the file already
        # held belongs to an earlier session and would stop this one at once.
        self.watched_to = 0
        if self.tag_log:
            # Same reason as below: what the file already holds is an older
            # session, and sampling on its scenes would describe nothing.
            try:
                self.tag_read_to = os.path.getsize(self.tag_log)
                say(f"watching {self.tag_log} for scene changes")
            except OSError as error:
                say(f"could not read the tag log: {error}")
                self.tag_log = None
        if self.watch_log:
            try:
                self.watched_to = os.path.getsize(self.watch_log)
                say(f"watching {self.watch_log} for trouble")
            except OSError as error:
                say(f"cannot watch the game's log: {error}")
                self.watch_log = None
        self.cache_limit = None
        if self.game_pid is None:
            say(f"no process is listening on {options.tjs_port}; "
                "memory will not be recorded")

    def route_name(self):
        return self.route.get("name", f"route {self.route_index + 1}")

    def stop_if_the_game_is_gone(self):
        """Ends the run once there is no game left to play.

        Nothing else notices. The client keeps asking, this keeps answering, and
        the run goes on against a game that closed -- once for an hour, holding
        the port, so the next run bound beside it and the client went on talking
        to this one. What looked like a broken driver was a finished one nobody
        had stopped.
        """
        if self.game_pid is None or self.stopping:
            return
        if self.turns % GAME_CHECK_EVERY_TURNS:
            return
        if process_is_alive(self.game_pid):
            return
        # The process this run started against is gone, which is not the same as
        # there being no game: restarting it is a normal thing to do, and the
        # chain does it between routes on purpose. So the port is asked again --
        # whoever holds it now is the game to follow.
        replacement = game_pid(self.options.tjs_port)
        if replacement is not None and replacement != self.game_pid:
            say(f"the game was replaced: pid {self.game_pid} -> {replacement}; "
                "following the new one")
            self.game_pid = replacement
            return
        self.stopping = True
        say("the game is gone; stopping the run")
        if self.on_stop:
            self.on_stop()

    def say_run_header(self):
        """Writes down what this run is, before it starts running.

        A run's numbers are only worth comparing against another run's when the
        two were made under the same conditions -- and those conditions were
        being kept in someone's head. The log records what happened; this
        records what it happened under.
        """
        options = self.options
        routes = ", ".join(r.get("name", "(unnamed)") for r in self.routes)
        say(f"run: mode={options.mode} fast={bool(options.fast)} "
            f"routes=[{routes or 'none'}] tjs_port={options.tjs_port} "
            f"engine_port={options.port}")
        say(f"run: watch_tag_log={bool(self.tag_log)} "
            f"watch_game_log={bool(self.watch_log)} "
            f"memory_limit={options.memory_limit or 'off'}")
        try:
            version = send_tjs(options.tjs_port, "neuroGetVersion()").strip()
        except OSError:
            version = "unknown"
        directory = game_directory(options.tjs_port)
        stamp = build_stamp(os.path.join(directory, "Neuro-Integration.dll")) \
            if directory else None
        say(f"run: plugin version={version} dll=({stamp or 'not found'})")
        if options.gary_exe:
            version = file_version(options.gary_exe) or "version unknown"
            say(f"run: gary=({version}, "
                f"{build_stamp(options.gary_exe) or 'not found'})")

    def note_scene_changes(self):
        """Takes a snapshot whenever the game says it entered another scene.

        The step in memory happened on one scene, and a sample every 500 turns
        could only place it inside a two-minute window. The game already
        announces each scene in its tag log, so the sample goes where the
        evidence is rather than on a timer.
        """
        if not self.tag_log or self.stopping:
            return
        try:
            size = os.path.getsize(self.tag_log)
            if size <= self.tag_read_to:
                return
            with open(self.tag_log, encoding="utf-8", errors="replace") as handle:
                handle.seek(self.tag_read_to)
                fresh = handle.read()
            self.tag_read_to = size
        except OSError as error:
            say(f"could not read the tag log: {error}")
            self.tag_log = None
            return
        # Only the last one: several scenes can land between two polls, and a
        # snapshot per line would describe the same moment several times over.
        scene = None
        for line in fresh.splitlines():
            if line.startswith("[scenario] "):
                scene = line[len("[scenario] "):].strip()
        if scene:
            self.note_memory(f"scene {scene}")

    def stop_on_trouble(self):
        """Ends the run while the game is still there to look at.

        The point is not to save the run -- it is already lost -- but to stop
        before the game does, so it can be examined alive: attached to with a
        debugger, or dumped. Once it has shown its error box and exited there is
        nothing left but a log.

        The signal comes from the game's own log. Before the run that died, the
        engine spent three minutes failing to allocate for Direct3D, hundreds of
        lines of it, and only then hit an image it could not load. Three minutes
        is plenty of warning; nobody was reading the log at the time.

        Memory is the second signal, and the weaker one: it says the room is
        running out, not that anything has gone wrong yet. A 32-bit process has
        about two gigabytes of address space, and the failures above began
        around one.
        """
        if self.stopping:
            return
        if self.turns % GAME_CHECK_EVERY_TURNS:
            return

        reason = None
        if self.watch_log:
            try:
                size = os.path.getsize(self.watch_log)
                if size > self.watched_to:
                    with open(self.watch_log, encoding="utf-8",
                              errors="replace") as handle:
                        handle.seek(self.watched_to)
                        fresh = handle.read()
                    self.watched_to = size
                    for phrase in TROUBLE_IN_THE_GAME_LOG:
                        if phrase in fresh:
                            reason = f"the game's log says: {phrase}"
                            break
            except OSError as error:
                # Losing the log is not a reason to stop watching the memory.
                say(f"could not read the game's log: {error}")
                self.watch_log = None

        if reason is None and self.memory_limit and self.game_pid:
            usage = process_memory(self.game_pid)
            if usage and usage[1] >= self.memory_limit:
                reason = (f"the game holds {usage[1]:.0f}MB private, "
                          f"at or past the {self.memory_limit}MB mark")

        if reason is None:
            return

        self.stopping = True
        say(f"stopping while the game is still alive: {reason}")
        self.note_memory("trouble")
        if self.dump_with and self.game_pid:
            self.take_a_dump()
        elif self.game_pid:
            say(f"the game is still running as pid {self.game_pid}; "
                "attach to it now, or pass --dump-with to capture one here")
        self.restore_speed()
        if self.on_stop:
            self.on_stop()

    def take_a_dump(self):
        """Captures the game's memory while it is still there to capture."""
        say(f"writing a dump of pid {self.game_pid} with {self.dump_with}")
        try:
            done = subprocess.run(
                [self.dump_with, "-accepteula", "-ma", str(self.game_pid)],
                capture_output=True, text=True, timeout=300)
            tail = (done.stdout or done.stderr or "").strip().splitlines()
            say("dump: " + (tail[-1] if tail else f"exit {done.returncode}"))
        except (OSError, subprocess.SubprocessError) as error:
            say(f"could not write a dump: {error}")

    def note_memory(self, occasion):
        """Records how much memory the game is holding, and at what point.

        A run that ends in a failed image load says nothing about when the
        memory went: by then it is simply gone. The number is only useful as a
        curve, so it is taken at the moments a curve can be read against --
        every choice, every route, and at a steady interval in between.

        The graphic cache limit rides along because it is the ceiling the engine
        set for itself from the machine's memory, and an image load is exactly
        what failed.
        """
        if self.game_pid is None:
            return
        usage = process_memory(self.game_pid)
        if usage is None:
            return
        working, private = usage
        cache = ""
        if self.cache_limit is not None:
            cache = f" cache_limit={self.cache_limit}"
        # The three axes travel together because they answer different halves of
        # the same question: bytes say how much is held, the largest free block
        # says whether a big image would still fit -- which is what actually
        # failed -- and the layer count says whether the growth is more objects
        # or the same ones holding more.
        block = largest_free_block(self.game_pid)
        free = f" free_block={block:.0f}MB" if block is not None else ""
        layers = layer_count(self.options.tjs_port)
        tree = f" layers={layers}" if layers is not None else ""
        say(f"memory: {occasion} working={working:.0f}MB private={private:.0f}MB"
            f"{free}{tree}{cache} turn={self.turns}")

    def load_route(self):
        """Takes up the route at the current index, from its first decision."""
        self.route = self.routes[self.route_index] if self.routes else {}
        # How this route begins. The file may say it, and the command line
        # overrides it -- which is what lets the true ending, a run with no
        # decisions at all, be played without a file.
        self.start_action = (self.options.start
                             or self.route.get("start")
                             or ("start_new_game"
                                 if self.route.get("start_new_game") else None))
        self.decisions = list(self.route.get("choices", []))
        self.taken = 0
        # Indexes already refused for the decision in hand, so a second match
        # for the same text can be tried instead of the run stopping. The
        # scenario does declare one choice twice -- b402 carries two entries
        # reading "Pick up the shoe.", one of them disabled, told apart by a
        # flag -- but the engine renders only one of the two, and no run has
        # ever seen a refused index. Kept as a guard, not as a known case.
        self.rejected = set()
        self.started = self.start_action is None
        self.remembered_options = None
        # The end of a route is read off the title screen, so a route that
        # never left it has not ended -- it never began. Without this a start
        # that did not take (a True End the profile has not unlocked, a refused
        # press) reads as a finished route: the chain would skip what is left
        # and close the game, having played nothing.
        self.left_title = False

    def next_route(self):
        """Moves on to the route after this one, or reports there is none."""
        if self.route_index + 1 >= len(self.routes):
            return False
        finished = self.route_name()
        self.note_memory(f"{finished} finished")
        self.route_index += 1
        self.load_route()
        say(f"route {finished} finished; starting {self.route_name()} "
            f"({len(self.decisions)} choice(s))")
        return True

    def choose_for_route(self, actions, schema, messages):
        """The route's own move, or (None, why) to fall through to the mode."""
        # Noted on every turn, not only when the choice action is on offer: the
        # list reaches the context before the action is registered, and by the
        # time it is, a trimmed prompt may no longer carry it.
        seen = latest_choice(messages)
        if seen:
            self.remembered_options = seen

        if not self.started and self.start_action in actions:
            self.started = True
            return (command_for(self.start_action, actions),
                    f"route: starting with {self.start_action}")

        if "select_choice" in actions:
            options = self.remembered_options
            if not options:
                return None, self.diagnose_missing_choice()
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
        at_title = "start_new_game" in actions and "exit_game" in actions
        if not at_title:
            self.left_title = True
        elif not self.left_title:
            # Still on the title the route started from: nothing has been
            # played yet, so this is not the title that ends it.
            return None, ROUTE_IDLE
        # Back at the title with every decision made: the route has run out.
        # Waiting for the title is what makes this work without knowing where
        # a route ends -- the game says so by offering a new game again.
        #
        # The next route is taken up here rather than by falling through to the
        # start branch above, which would spend a turn answering the title with
        # whatever the mode picks before the chain moved on.
        if route_done and at_title and self.next_route():
            if self.start_action in actions:
                self.started = True
                return (command_for(self.start_action, actions),
                        f"route: starting {self.route_name()} with {self.start_action}")
            # A route that names no way in, or names one the title is not
            # offering: say so instead of pressing something else. The most
            # likely cause is the True End, which the title hides until the
            # profile has unlocked it.
            return None, (f"route: {self.route_name()} asks for "
                          f"{self.start_action!r}, which the title is not offering")

        if route_done and self.finish:
            # Quitting asks for confirmation, and the schema for answering it
            # takes a boolean. Filled from the schema alone that boolean comes
            # out false, which cancels the dialog -- and the run then loops:
            # quit, decline, back to the title, quit again.
            if "answer_dialog" in actions and self.leaving:
                return (command_for("answer_dialog", actions, {"confirm": True}),
                        "route: confirming the quit")
            if "start_new_game" in actions and "exit_game" in actions:
                self.leaving = True
                self.note_memory("finished")
                self.restore_speed()
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
        self.note_memory(f"{self.route_name()} choice {self.taken + 1}")
        self.pending = False
        self.pending_list = None
        self.remembered_options = None
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

    def restore_speed(self):
        """Puts the player's own speed settings back.

        The game writes its settings to disk when it closes, so a run that ends
        with the quit action and leaves the speeds turned up makes that change
        permanent -- and the save folder is shared with the ordinary install, so
        it is the player's settings that change. Restoring is therefore part of
        finishing, not a courtesy.
        """
        if not self.options.fast or self.speed_restored:
            return
        self.speed_restored = True
        try:
            with open(REPO_TJS_SLOW, encoding="utf-8") as handle:
                say("speed restored: " + send_tjs(self.options.tjs_port, handle.read()).strip())
        except OSError as error:
            say(f"could not restore the speed settings: {error}")

    def diagnose_missing_choice(self):
        """Says why the route is stuck when no choice list has been seen.

        The game is asked, but only to name the problem -- never to answer with.
        The options reach Neuro through the context and nowhere else, so a
        choice the game is showing while the context stays silent is a defect
        in what we send her. Reading the list off the game here would answer
        the turn and hide exactly that.
        """
        try:
            showing = send_tjs(self.options.tjs_port,
                               "(function(){ try { return kag.selectLayer.selects.count; }"
                               " catch (e) { return 0; } })()").strip()
        except OSError:
            return "route: no choice list in the context, and the game could not be asked"
        if showing and showing != "0":
            return (f"route: the game is showing {showing} options but none of them"
                    " reached the context -- that is a defect, not a stall")
        return "route: no choice list in the context, and the game shows none either"

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
            if any(refusal in text for refusal in ADVANCE_REFUSED):
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
        if self.turns == 1:
            # Read once the game is answering. It is the ceiling the engine set
            # for its image cache from the machine's memory, so it belongs with
            # the first reading rather than in a note somewhere.
            try:
                self.cache_limit = send_tjs(self.options.tjs_port,
                                            "System.graphicCacheLimit").strip()
            except OSError:
                self.cache_limit = None
            self.note_memory("start")
        elif not self.tag_log and self.turns % MEMORY_EVERY_TURNS == 0:
            # Only when scene changes are not being watched. Both at once would
            # put two readings on the curve for the same stretch, and the timed
            # one says less: it lands wherever the count falls, not where
            # anything happened.
            self.note_memory("running")
        self.stop_if_the_game_is_gone()
        self.stop_on_trouble()
        self.note_scene_changes()
        actions = offered_actions(schema)
        self.arm_auto_mode(actions)
        if self.options.action in actions:
            self.note_refusals(messages)

        if self.decisions or self.route or self.start_action or self.finish:
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
        # The way to end a run that was started in the background. Windows has
        # no signal a stranger can send to such a process, so the stop has to
        # arrive over something the process is already listening on -- and this
        # server is it. Ending here rather than by killing the process is what
        # gets the player's speed settings put back.
        if self.path.rstrip("/").endswith("/stop"):
            self._json(200, {"stopping": True})
            threading.Thread(target=self.server.shutdown, daemon=True).start()
            return
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
    parser.add_argument("--route", nargs="+", metavar="FILE",
                        help="one or more JSON route files: the choices to make, "
                             "in order. Several are played one after another in "
                             "the order given, each starting from the title the "
                             "previous one came back to")
    parser.add_argument("--start", choices=("start_new_game", "start_true_end"),
                        help="what to press at the title; overrides the route file. "
                             "The true ending takes no choices, so it needs no route. "
                             "Only for a single route: in a chain each route names "
                             "its own way in")
    parser.add_argument("--exit-when-done", action="store_true",
                        help="close the game once the run is over")
    parser.add_argument("--watch-game-log", metavar="FILE",
                        help="the game's game_debug_log.txt. The run stops on the "
                             "first sign of trouble in it, leaving the game alive "
                             "to be attached to")
    parser.add_argument("--watch-tag-log", metavar="FILE",
                        help="the game's neuro_tag_log.txt. A memory snapshot is "
                             "taken on every scene change it reports, which is "
                             "what makes the curve fine enough to name a scene")
    parser.add_argument("--gary-exe", metavar="FILE",
                        help="path to the gary.exe this run is driven by. Only "
                             "for the run header: every build calls itself the "
                             "same version, so what identifies one is the file")
    parser.add_argument("--memory-limit", type=int, default=0, metavar="MB",
                        help="stop when the game holds this many megabytes of "
                             "private bytes; 0 means never. The game is 32-bit, so "
                             "it runs out of address space around 2000")
    parser.add_argument("--dump-with", metavar="PROCDUMP",
                        help="path to procdump: writes a full dump of the game "
                             "when a run stops on trouble")
    parser.add_argument("--fast", action="store_true",
                        help="turn the game's speed knobs up; safe here because "
                             "nothing is waiting on a model")
    options = parser.parse_args()

    routes = []
    for path in options.route or []:
        with open(path, encoding="utf-8") as handle:
            routes.append(json.load(handle))
    if options.start and len(routes) > 1:
        parser.error("--start applies to one route; in a chain each route names "
                     "its own start")
    for route in routes:
        say(f"route {route.get('name', '(unnamed)')}: "
            f"{len(route.get('choices', []))} choice(s)")
    if len(routes) > 1:
        say("chain: " + " -> ".join(r.get("name", "(unnamed)") for r in routes))

    # Windows lets a second listener bind a port another process is already on,
    # so starting a run while an older driver is still alive does not fail --
    # it succeeds, the client keeps talking to the old process, and the new one
    # sits there having received nothing. That looked like a broken driver: the
    # game was acting on decisions from a route that had ended hours before.
    probe = socket.socket()
    probe.settimeout(1.0)
    try:
        if probe.connect_ex(("127.0.0.1", options.port)) == 0:
            say(f"something is already serving on {options.port}; "
                "stop the driver that is still running, or pass --port")
            sys.exit(1)
    finally:
        probe.close()

    Handler.engine = Engine(options, routes)
    Handler.engine.say_run_header()
    prepare_game(options)

    server = HTTPServer(("127.0.0.1", options.port), Handler)
    # Same shutdown the stop endpoint uses, from a thread for the same reason:
    # it is called while a request is being served.
    Handler.engine.on_stop = lambda: threading.Thread(
        target=server.shutdown, daemon=True).start()
    say(f"scripted engine on http://127.0.0.1:{options.port}/v1 in {options.mode} mode")
    say("add it in Gary as an OpenAI-compatible engine with that Server URL")
    # Asking the server to stop, rather than letting an exception unwind out of
    # whatever it was doing, is what makes the restore below reliable: the run
    # ends between requests instead of in the middle of one.
    #
    # SIGBREAK is here because it is the only one a Windows console can send to
    # a process it did not start interactively, so a run started in the
    # background still has a way to end properly. A forced kill has none: it
    # leaves the speeds turned up, and they have to be put back by hand.
    def stop(signum, frame):
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGINT, stop)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, stop)
    signal.signal(signal.SIGTERM, stop)

    try:
        server.serve_forever()
        say("stopped")
    except KeyboardInterrupt:
        say("stopped")
    finally:
        # Also on the way out of an interrupted run: the speeds are the
        # player's, and leaving them turned up outlives this process.
        Handler.engine.restore_speed()


if __name__ == "__main__":
    main()
