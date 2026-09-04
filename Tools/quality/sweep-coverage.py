"""Compare what a scene sweep heard against what the scenarios declare.

Inputs are given on the command line, so the tool knows about file shapes and
not about anyone's folders:

    python sweep-coverage.py --index occurrences.csv sweep-*.tsv

With no arguments it looks in the current directory for sweep-*.tsv and
scenario-audio-occurrences.csv.

The index is a CSV with Type, Name, Scene and Label columns; a sweep is a TSV
written by audio-sweep.ps1, with scene, kind, file and (since the scene became
per-event) entry.

Three things the comparison has to get right:
  * a branch entry is the same scene -- b402.ks*402_b folds into b402;
  * occurrences under a *dummy label are unreachable from *start;
  * names are compared as sets, since the polled sources log on change rather
    than per playback.

Voice has no inventory in the index, so it is counted but not scored.
"""

import collections
import csv
import glob
import io
import os
import sys

SCORED = ("bgm", "se", "loopse")
DEFAULT_INDEX = "scenario-audio-occurrences.csv"


def scene_of(raw):
    return (raw or "").split("*")[0].replace(".ks", "")


def load_declared(path):
    every = collections.defaultdict(set)
    reachable = collections.defaultdict(set)
    with io.open(path, encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            name = row["Name"].lower()
            every[row["Scene"]].add(name)
            if not row["Label"].lower().startswith("*dummy"):
                reachable[row["Scene"]].add(name)
    return every, reachable


def load_heard(paths):
    heard = collections.defaultdict(set)
    voices = collections.defaultdict(set)
    order = []
    for path in paths:
        with io.open(path, encoding="utf-8") as handle:
            for row in csv.DictReader(handle, delimiter="\t"):
                name = (row.get("file") or "").strip().lower()
                if not name:
                    continue
                scene = scene_of(row.get("scene"))
                if not order or order[-1] != scene:
                    order.append(scene)
                if row.get("kind") == "voice":
                    voices[scene].add(name)
                elif row.get("kind") in SCORED:
                    heard[scene].add(name)
    return heard, voices, order


def score(declared, heard):
    total = hit = 0
    for scene, names in declared.items():
        total += len(names)
        hit += len(names & heard.get(scene, set()))
    return hit, total


def parse_args(argv):
    index, sweeps = None, []
    i = 1
    while i < len(argv):
        if argv[i] == "--index" and i + 1 < len(argv):
            index, i = argv[i + 1], i + 2
        else:
            sweeps.append(argv[i])
            i += 1
    if not sweeps:
        sweeps = [p for p in sorted(glob.glob("sweep-*.tsv"))
                  if "choices" not in os.path.basename(p)]
    return index or DEFAULT_INDEX, sweeps


def main(argv):
    index, sweeps = parse_args(argv)
    if not sweeps:
        print(__doc__)
        return 1
    for path in [index] + sweeps:
        if not os.path.exists(path):
            print("not found: %s" % path)
            return 1

    every, reachable = load_declared(index)
    heard, voices, order = load_heard(sweeps)

    print("sweeps: %s" % ", ".join(os.path.basename(p) for p in sweeps))
    print("scenes declared: %d, visited: %d" % (len(every), len(heard)))
    never = sorted(set(every) - set(heard))
    if never:
        print("NEVER VISITED: %s" % ", ".join(never))

    for title, declared in (("all labels", every), ("reachable only", reachable)):
        hit, total = score(declared, heard)
        print("%-16s %d/%d = %.1f%%" % (title, hit, total, 100.0 * hit / total))

    print("\n%-8s %6s %6s %6s  %s" % ("scene", "want", "got", "cover", "missing"))
    for scene in sorted(reachable):
        want = reachable[scene]
        got = want & heard.get(scene, set())
        pct = 100.0 * len(got) / len(want) if want else 100.0
        print("%-8s %6d %6d %5.0f%%  %s"
              % (scene, len(want), len(got), pct, ", ".join(sorted(want - got))))

    # A file missed in one scene but heard in another is one the capture can
    # see, so its absence is about the path taken. One never heard anywhere
    # would mean a blind spot, which is what this line watches for.
    everywhere = set()
    for names in heard.values():
        everywhere |= names
    missed = set()
    for scene, want in reachable.items():
        missed |= want - heard.get(scene, set())
    blind = sorted(f for f in missed if f not in everywhere)
    print("\nmissed somewhere: %d distinct" % len(missed))
    print("never heard at all: %d%s"
          % (len(blind), (" -> " + ", ".join(blind)) if blind else ""))

    # Sweeps taken before the collector stamped each event with its own scene
    # can file the tail of a batch under the scene the game had already left.
    following = {}
    for i, scene in enumerate(order[:-1]):
        following.setdefault(scene, order[i + 1])
    print("\nheard but not declared:")
    for scene in sorted(heard):
        for name in sorted(heard[scene] - every.get(scene, set())):
            nxt = following.get(scene)
            tag = ("boundary: declared in next scene %s" % nxt
                   if nxt and name in every.get(nxt, set())
                   else "not declared anywhere near")
            print("   %-16s %-6s %s" % (name, scene, tag))

    print("\nvoice: %d distinct lines over %d scenes (not scored)"
          % (sum(len(v) for v in voices.values()), len(voices)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
