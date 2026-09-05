"""Reconstructs what a run looked like at one moment.

Three processes keep three separate records of the same session and each sees
half of it: the plugin's tag log holds what the game reported, the client's run
log holds what actually went over the wire, and neither says what the other was
doing. The question worth asking of them is not "what happened" -- each answers
that alone -- but "what did the whole picture look like at 21:01:42", which is
what decides things like whether a looping effect should announce that it
stopped, or whether a field is noise that Neuro never needed.

A moment is given either as a timestamp or as an anchor -- "the second choice",
"the line that says shoes" -- because finding one by hand in an hour of logs is
the part that stops anyone from asking.

The action list is rebuilt from deltas rather than read anywhere: registering
and unregistering are events, so what was available at a moment is what the
deltas add up to. A connection resets it. Registering a name that is already
there overwrites it, which is what the v2 protocol does; a v1 game keeps the
existing one instead, and this does not tell the two apart.

Timestamps come from different clocks and are only as good as they are: the
plugin stamps whole polls, so everything in one 200 ms tick shares a stamp, and
ordering inside a tick is not knowable. Lines are shown in the order each source
recorded them and never interleaved more finely than that.
"""

import argparse
import json
import os
import re
from datetime import datetime, timedelta

TAG_LINE = re.compile(r"^(?P<stamp>\S+) \[(?P<field>[^\]]+)\] ?(?P<value>.*)$")


def parse_stamp(text):
    """A tag log stamp, or None. Local time with an offset, as the plugin writes it."""
    try:
        return datetime.fromisoformat(text)
    except ValueError:
        return None


def load_tag_log(path):
    """The game's own record: field changes and semantic events, in order.

    A directory is taken as an instance's log directory. Nothing is searched
    for: the file rotates into Logs\\ under a dated name and a second copy of
    the game keeps its own directory, so a guess here eventually opens the other
    instance's log -- which does not look wrong, it looks like a quiet run.
    """
    if os.path.isdir(path):
        path = os.path.join(path, "neuro_tag_log.txt")
    rows = []
    unstamped = 0
    banner = ""
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("#"):
                # Which process, which port, which config -- said out loud so a
                # log from the wrong instance is noticed before it is believed.
                if not banner and line.startswith("# instance"):
                    banner = line
                continue
            match = TAG_LINE.match(line)
            if not match:
                continue
            at = parse_stamp(match.group("stamp"))
            if at is None:
                unstamped += 1
                continue
            rows.append({
                "at": at,
                "field": match.group("field"),
                "value": match.group("value"),
            })
    return rows, unstamped, banner


def load_run(directory):
    """The client's record, as three streams of one JSON object per line."""
    streams = {}
    for name in ("events", "context", "actions"):
        path = os.path.join(directory, f"{name}.jsonl")
        rows = []
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        entry = json.loads(line)
                    except json.JSONDecodeError:
                        # A run that ended with the process being killed leaves
                        # a truncated last line. That is one lost record, not a
                        # lost file.
                        continue
                    entry["at"] = parse_stamp(entry.get("t", ""))
                    rows.append(entry)
        streams[name] = rows
    return streams


def actions_at(action_rows, moment):
    """The actions the game was offering at `moment`, and where each came from."""
    known = {}
    for row in action_rows:
        if row["at"] is None or row["at"] > moment:
            break
        key = row.get("key")
        data = row.get("data") or {}
        if key in ("api/game/connected", "api/game/disconnected"):
            known.clear()
            continue
        if key == "api/game/register":
            for action in data.get("actions") or []:
                name = action.get("name")
                if name:
                    known[name] = {"since": row["at"], "action": action}
            continue
        if key == "api/game/unregister":
            for name in data.get("action_names") or []:
                known.pop(name, None)
    return known


def fields_at(tag_rows, moment):
    """Every context field's last reported value at `moment`.

    The monitor only reports a field when it changes, so the state at a moment
    is the accumulation of everything before it -- which is exactly why reading
    the log by eye answers the wrong question.
    """
    state = {}
    for row in tag_rows:
        if row["at"] > moment:
            break
        if row["field"] == "event":
            continue
        state[row["field"]] = {"value": row["value"], "at": row["at"]}
    return state


def find_moment(args, tag_rows, streams):
    """The moment to describe, from a timestamp or an anchor."""
    if args.at:
        moment = parse_stamp(args.at)
        if moment is None:
            raise SystemExit(f"unreadable timestamp: {args.at}")
        return moment, "given"

    if args.choice:
        choices = [r for r in streams["context"]
                   if "A choice appeared" in str((r.get("data") or {}).get("message", ""))]
        if not choices:
            raise SystemExit("no choice appeared in this run")
        if args.choice > len(choices):
            raise SystemExit(f"the run has {len(choices)} choice(s), not {args.choice}")
        chosen = choices[args.choice - 1]
        return chosen["at"], (chosen.get("data") or {}).get("message", "")

    if args.text:
        needle = args.text.casefold()
        for row in tag_rows:
            if row["field"] == "event" and needle in row["value"].casefold():
                return row["at"], row["value"]
        raise SystemExit(f"no event in the tag log contains {args.text!r}")

    raise SystemExit("say which moment: --at, --choice or --text")


def window(rows, moment, span, key=lambda r: r["at"]):
    low, high = moment - span, moment + span
    return [r for r in rows if key(r) is not None and low <= key(r) <= high]


def show_time(at):
    return at.strftime("%H:%M:%S.") + f"{at.microsecond // 1000:03d}"


def describe(args, tag_rows, streams, moment, why):
    span = timedelta(seconds=args.window)
    print(f"=== {show_time(moment)}  ({why})")
    print(f"    +/- {args.window}s")

    print("\n--- what the game was reporting")
    state = fields_at(tag_rows, moment)
    for name in sorted(state):
        entry = state[name]
        age = (moment - entry["at"]).total_seconds()
        print(f"  {name:14s} {entry['value']:<48s} set {age:6.1f}s earlier")
    if not state:
        print("  (nothing -- no field had been reported yet)")

    print("\n--- actions Neuro could take")
    offered = actions_at(streams["actions"], moment)
    for name in sorted(offered):
        age = (moment - offered[name]["since"]).total_seconds()
        description = (offered[name]["action"].get("description") or "").strip()
        print(f"  {name:22s} registered {age:7.1f}s earlier  {description[:60]}")
    if not offered:
        print("  (none -- nothing was registered, or the game had disconnected)")

    print("\n--- context messages sent to Neuro")
    for row in window(streams["context"], moment, span):
        data = row.get("data") or {}
        message = str(data.get("message", "")).replace("\n", " / ")
        if not message:
            continue
        mark = " " if data.get("silent", True) else "!"
        print(f" {mark}{show_time(row['at'])}  {message[:150]}")

    print("\n--- what the game logged")
    for row in window(tag_rows, moment, span):
        value = row["value"].replace("\t", " | ")
        print(f"  {show_time(row['at'])}  [{row['field']}] {value[:150]}")

    print("\n--- what the client did")
    interesting = ("api/game/act/actor", "api/game/action_result", "api/game/force",
                   "api/game/register", "api/game/unregister", "app/scheduler/act/error",
                   "app/diagnostics/triggered")
    # The action stream is a filtered copy of the event stream, so the same
    # event arrives twice; the id is what tells them apart from two things that
    # happened to look alike.
    seen = set()
    for row in window(streams["actions"] + streams["events"], moment, span):
        if row.get("key") not in interesting:
            continue
        if row.get("id") in seen:
            continue
        seen.add(row.get("id"))
        data = row.get("data") or {}
        act = data.get("act") or {}
        detail = act.get("name") or ",".join(data.get("action_names") or []) or ""
        if row["key"] == "api/game/register":
            detail = ",".join(a.get("name", "?") for a in (data.get("actions") or []))
        outcome = ""
        if "success" in data:
            outcome = " ok" if data["success"] else " REFUSED"
        message = str(data.get("message", ""))
        print(f"  {show_time(row['at'])}  {row['key']:26s} {detail[:60]}{outcome} {message[:40]}")


def list_choices(streams):
    choices = [r for r in streams["context"]
               if "A choice appeared" in str((r.get("data") or {}).get("message", ""))]
    if not choices:
        print("no choice appeared in this run")
        return
    for index, row in enumerate(choices, 1):
        print(f"{index:3d}  {show_time(row['at'])}  {(row.get('data') or {}).get('message', '')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True,
                        help="a client run directory holding events/context/actions.jsonl")
    parser.add_argument("--tag-log", required=True,
                        help="the plugin's neuro_tag_log.txt, written with log_timestamps = on")
    parser.add_argument("--at", help="the moment, as a timestamp from either log")
    parser.add_argument("--choice", type=int, help="the moment the Nth choice appeared")
    parser.add_argument("--text", help="the moment an event first mentions this text")
    parser.add_argument("--list-choices", action="store_true",
                        help="show the choices in this run and stop")
    parser.add_argument("--window", type=float, default=5.0,
                        help="how many seconds either side to show (default 5)")
    args = parser.parse_args()

    streams = load_run(args.run)
    tag_rows, unstamped, banner = load_tag_log(args.tag_log)
    print(banner or "note: the tag log names no instance -- an older build wrote it")
    if unstamped:
        print(f"note: {unstamped} tag log line(s) carry no timestamp -- "
              f"set log_timestamps = on in the instance config for a full picture\n")

    if args.list_choices:
        list_choices(streams)
        return

    moment, why = find_moment(args, tag_rows, streams)
    describe(args, tag_rows, streams, moment, why)


if __name__ == "__main__":
    main()
