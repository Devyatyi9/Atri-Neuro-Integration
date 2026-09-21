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


def load_driver_log(path):
    """The driver's own record: what it decided, and what the game weighed.

    A fourth source, and the only one that knows anything outside the two
    processes the other three come from -- the memory readings are taken of the
    game from outside it, so nothing in the client's record or the game's own
    log can carry them.
    """
    if not path:
        return []
    rows = []
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.split(" ", 1)
        if len(parts) != 2:
            continue
        at = parse_stamp(parts[0])
        if at is None:
            continue
        rows.append({"at": at, "text": parts[1].rstrip("\n")})
    return rows


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


def describe(args, tag_rows, streams, driver_rows, moment, why):
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

    if driver_rows:
        # Every turn would bury the rest of the output, so only the lines the
        # driver writes about something happening: memory readings, route
        # decisions, whatever it could not do.
        noise = re.compile(r"^turn \d+: ")
        shown = [row for row in window(driver_rows, moment, span)
                 if not noise.match(row["text"])]
        print("\n--- what the driver saw")
        if not shown:
            # The nearest reading either side, since a moment rarely lands on
            # one and "no memory here" is not the useful answer.
            before = [r for r in driver_rows
                      if r["at"] <= moment and r["text"].startswith("memory:")]
            after = [r for r in driver_rows
                     if r["at"] > moment and r["text"].startswith("memory:")]
            for label, rows in (("last before", before[-1:]), ("first after", after[:1])):
                for row in rows:
                    gap = abs((row["at"] - moment).total_seconds())
                    print(f"  {show_time(row['at'])}  {row['text'][:120]}"
                          f"   ({label}, {gap:.0f}s away)")
            if not before and not after:
                print("  (nothing)")
        for row in shown:
            print(f"  {show_time(row['at'])}  {row['text'][:150]}")


def list_choices(streams):
    choices = [r for r in streams["context"]
               if "A choice appeared" in str((r.get("data") or {}).get("message", ""))]
    if not choices:
        print("no choice appeared in this run")
        return
    for index, row in enumerate(choices, 1):
        print(f"{index:3d}  {show_time(row['at'])}  {(row.get('data') or {}).get('message', '')}")



# --- the protocol checks a finished run can answer ------------------------
#
# These used to live in prototype/tooling/neuro_harness_test.py and were asked
# of a harness that models the protocol rather than implements it. That model
# once missed a real defect -- actions the game believed it had dropped stayed
# callable, because `actions/register` is additive and nothing said so -- and it
# was the real client that found it.
#
# So they are asked here instead, of a record made by the real client. What
# cannot move stays in the harness: every check that needs a server willing to
# send something malformed, which a correct one never will.


class Checker:
    """Collects pass/fail lines so one bad check does not hide the rest."""

    def __init__(self):
        self.results = []

    def check(self, name, ok, detail=""):
        self.results.append((bool(ok), name, detail))

    def report(self):
        width = max(len(name) for _, name, _ in self.results)
        failed = 0
        for ok, name, detail in self.results:
            mark = "PASS" if ok else "FAIL"
            failed += 0 if ok else 1
            print(f"  {mark}  {name:{width}s}  {detail}")
        print()
        print(f"{len(self.results) - failed} of {len(self.results)} checks passed")
        return failed


def percentile(values, share):
    ordered = sorted(values)
    if not ordered:
        return 0
    return ordered[min(len(ordered) - 1, int(round(share * (len(ordered) - 1))))]


def run_checks(streams):
    """Asks the run whether the protocol held, and says so line by line."""
    checker = Checker()
    actions = streams["actions"]

    startups = [r for r in actions if r.get("key") == "api/game/startup"]
    checker.check("startup announced the game", bool(startups),
                  f"{len(startups)} startup message(s)")

    registers = [r for r in actions if r.get("key") == "api/game/register"]
    unregisters = [r for r in actions if r.get("key") == "api/game/unregister"]

    unnamed, unshaped = 0, 0
    for row in registers:
        for action in (row.get("data") or {}).get("actions") or []:
            if not action.get("name"):
                unnamed += 1
            schema = action.get("schema")
            if schema is not None and schema.get("type") != "object":
                unshaped += 1
    checker.check("every registered action has a name", unnamed == 0,
                  f"{len(registers)} snapshot(s), {unnamed} unnamed")
    checker.check("every schema is an object", unshaped == 0,
                  f"{unshaped} with another type")

    duplicated = 0
    for row in registers:
        names = [a.get("name") for a in (row.get("data") or {}).get("actions") or []]
        if len(names) != len(set(names)):
            duplicated += 1
    checker.check("no snapshot repeats a name", duplicated == 0,
                  f"{duplicated} snapshot(s) with a repeat")

    # The registry the server would keep, rebuilt from the deltas. An
    # unregister that names something not active means the two sides disagree
    # about what is callable -- which is exactly the defect the harness missed.
    active, stale, unknown = set(), 0, 0
    for row in actions:
        key = row.get("key")
        data = row.get("data") or {}
        if key in ("api/game/connected", "api/game/disconnected"):
            active.clear()
        elif key == "api/game/register":
            for action in data.get("actions") or []:
                if action.get("name"):
                    active.add(action["name"])
        elif key == "api/game/unregister":
            for name in data.get("action_names") or []:
                if name in active:
                    active.discard(name)
                else:
                    unknown += 1
    checker.check("unregister only names active actions", unknown == 0,
                  f"{len(unregisters)} unregister(s), {unknown} naming something else")

    # Pairing. Every act must be answered once: not zero times, which is a lost
    # turn, and not twice, which is the duplicate-id defect seen from this side.
    sent, answered, extra = {}, {}, 0
    for row in actions:
        data = row.get("data") or {}
        act = (data.get("act") or {})
        if row.get("key") == "api/game/act/actor" and act.get("id"):
            sent[act["id"]] = row
        elif row.get("key") == "api/game/action_result" and act.get("id"):
            if act["id"] in sent:
                answered[act["id"]] = answered.get(act["id"], 0) + 1
            else:
                extra += 1
    unanswered = len(sent) - len(answered)
    repeated = sum(1 for count in answered.values() if count > 1)
    checker.check("every action was answered", unanswered == 0,
                  f"{len(sent)} sent, {unanswered} unanswered")
    checker.check("no action was answered twice", repeated == 0 and extra == 0,
                  f"{repeated} answered twice, {extra} result(s) for nothing sent")

    # What the client itself complained about. Gary implements the protocol, so
    # its own diagnostics are a stricter reading of the same run than anything
    # rebuilt here -- and until now nobody read them. Its keys divide the same
    # way our checks do: prot/ is the protocol broken, perf/ is it held but
    # slowly, and slow is measured below rather than failed here.
    diagnostics = {}
    for row in streams["events"]:
        if row.get("key") != "app/diagnostics/triggered":
            continue
        key = (((row.get("data") or {}).get("diag") or {}).get("key") or "")
        if key:
            diagnostics[key] = diagnostics.get(key, 0) + 1
    broken = {k: n for k, n in diagnostics.items() if k.startswith("prot/")}
    summary = ", ".join(f"{k} x{n}" for k, n in sorted(diagnostics.items()))
    checker.check("the client reported no protocol fault", not broken,
                  summary or "the client reported nothing")

    # Latency, against the client's own limit rather than one invented here:
    # it calls a result late past 500ms.
    LATE_MS = 500
    gaps = []
    pending = {}
    for row in actions:
        act = (row.get("data") or {}).get("act") or {}
        if not act.get("id"):
            continue
        if row.get("key") == "api/game/act/actor":
            pending[act["id"]] = row["ms"]
        elif row.get("key") == "api/game/action_result" and act["id"] in pending:
            gaps.append(row["ms"] - pending.pop(act["id"]))
    late = sum(1 for gap in gaps if gap > LATE_MS)
    share = late / len(gaps) * 100 if gaps else 0
    checker.check("results arrive inside the client's limit", share < 1.0,
                  f"median {percentile(gaps, 0.5):.0f}ms, p90 {percentile(gaps, 0.9):.0f}ms, "
                  f"{late} of {len(gaps)} past {LATE_MS}ms ({share:.1f}%)")

    return checker.report()

def spread(values):
    """min / median / max of a list, as one string."""
    if not values:
        return "none"
    ordered = sorted(values)
    middle = ordered[len(ordered) // 2]
    return f"{ordered[0]} / {middle} / {ordered[-1]}"


def numbers_in(text):
    """Every whole number in a piece of text, as a set."""
    return set(int(found) for found in re.findall(r"\d+", text or ""))


def run_stats(streams):
    """Measures the run instead of judging it.

    The checks next door ask whether the protocol held. These ask what the run
    was like -- how big the action set got, how long the descriptions ran, how
    much context went out -- because those are the numbers we kept measuring by
    hand, one question at a time, and then losing.
    """
    actions = streams["actions"]
    contexts = streams["context"]

    registers = [r for r in actions if r.get("key") == "api/game/register"]
    results = [r for r in actions if r.get("key") == "api/game/action_result"]
    sent = [r for r in actions if r.get("key") == "api/game/act/actor"]

    print("Action set")
    sizes = [len((r.get("data") or {}).get("actions") or []) for r in registers]
    print(f"  snapshots sent          {len(registers)}")
    print(f"  actions per snapshot    {spread(sizes)}  (min / median / max)")
    span = 0.0
    if registers and registers[-1]["at"] and registers[0]["at"]:
        span = (registers[-1]["at"] - registers[0]["at"]).total_seconds() / 60
    if span > 0:
        print(f"  snapshots per minute    {len(registers) / span:.1f}")

    # Descriptions, keyed by name: the same action is registered many times over
    # a run, and only its longest wording matters for the limit.
    longest = {}
    for row in registers:
        for action in (row.get("data") or {}).get("actions") or []:
            name = action.get("name")
            text = action.get("description") or ""
            if name and len(text) > len(longest.get(name, "")):
                longest[name] = text
    print()
    print("Descriptions")
    if longest:
        lengths = [len(t) for t in longest.values()]
        print(f"  actions seen            {len(longest)}")
        print(f"  characters              {spread(lengths)}")
        over = sorted(((len(t), n) for n, t in longest.items()), reverse=True)[:5]
        for size, name in over:
            print(f"    {size:5d}  {name}")
    else:
        print("  none registered")

    print()
    print("Context")
    silent = sum(1 for r in contexts
                 if (r.get("data") or {}).get("silent"))
    texts = [(r.get("data") or {}).get("message") or "" for r in contexts]
    said = [len(t) for t in texts if t]
    print(f"  messages                {len(contexts)}, {silent} silent")
    if said:
        print(f"  characters              {spread(said)}")

    print()
    print("Results")
    failed = sum(1 for r in results
                 if not (r.get("data") or {}).get("success"))
    print(f"  actions sent            {len(sent)}")
    print(f"  results                 {len(results)}, {failed} refused")

    # The pairing check: an answer that quotes a different number than the
    # request did. `open_edit_menu` with slot 19 once answered "slot 9", and
    # nothing in a stream of plausible numbers made that stand out.
    asked = {}
    for row in sent:
        act = (row.get("data") or {}).get("act") or {}
        raw = act.get("data")
        if not raw:
            continue
        try:
            fields = json.loads(raw)
        except (json.JSONDecodeError, TypeError):
            continue
        wanted = set(v for v in (fields or {}).values() if isinstance(v, int))
        if wanted:
            asked[act.get("id")] = (act.get("name"), wanted)

    mismatched = []
    for row in results:
        data = row.get("data") or {}
        act = data.get("act") or {}
        pair = asked.get(act.get("id"))
        if not pair or not data.get("success"):
            continue
        name, wanted = pair
        found = numbers_in(data.get("message"))
        if found and not (wanted & found):
            mismatched.append((name, sorted(wanted), sorted(found),
                               data.get("message")))
    print(f"  answers quoting another number  {len(mismatched)}")
    for name, wanted, found, message in mismatched[:5]:
        print(f"    {name}: asked {wanted}, answered {found} -- {message}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True,
                        help="a client run directory holding events/context/actions.jsonl")
    parser.add_argument("--tag-log",
                        help="the plugin's neuro_tag_log.txt, written with log_timestamps = on")
    parser.add_argument("--at", help="the moment, as a timestamp from either log")
    parser.add_argument("--choice", type=int, help="the moment the Nth choice appeared")
    parser.add_argument("--text", help="the moment an event first mentions this text")
    parser.add_argument("--stats", action="store_true",
                        help="measure the run: action set size, description "
                             "lengths, context volume, and answers that quote a "
                             "different number than the request did")
    parser.add_argument("--check", action="store_true",
                        help="ask the run whether the protocol held, and stop. "
                             "Exits non-zero if any check failed")
    parser.add_argument("--list-choices", action="store_true",
                        help="show the choices in this run and stop")
    parser.add_argument("--driver-log",
                        help="the scripted driver's own output, which carries the "
                             "memory readings and the route's decisions")
    parser.add_argument("--window", type=float, default=5.0,
                        help="how many seconds either side to show (default 5)")
    args = parser.parse_args()

    # The modes that place a moment in the game's own log need that log. The
    # ones that only read the client's record -- the measurements and the
    # protocol checks -- do not, and asking for it turned a two-second check
    # into a hunt for the right file.
    if not (args.stats or args.check) and not args.tag_log:
        parser.error("--tag-log is required except for --stats and --check")

    streams = load_run(args.run)
    # Measuring the client's own record needs nothing from the game's tag log,
    # and asking for a file that will not be read is a way to make a tool look
    # harder to use than it is.
    if args.stats:
        run_stats(streams)
        return

    # The same for the protocol checks: they read the client's record and
    # nothing else.
    if args.check:
        raise SystemExit(1 if run_checks(streams) else 0)

    tag_rows, unstamped, banner = load_tag_log(args.tag_log)
    print(banner or "note: the tag log names no instance -- an older build wrote it")
    if unstamped:
        print(f"note: {unstamped} tag log line(s) carry no timestamp -- "
              f"set log_timestamps = on in the instance config for a full picture\n")

    if args.list_choices:
        list_choices(streams)
        return

    driver_rows = load_driver_log(args.driver_log)

    moment, why = find_moment(args, tag_rows, streams)
    describe(args, tag_rows, streams, driver_rows, moment, why)


if __name__ == "__main__":
    main()
