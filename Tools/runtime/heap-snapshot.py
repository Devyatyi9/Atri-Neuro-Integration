"""What the process heaps hold, as one line.

`System.dumpHeap()` answers the half of the memory question the driver's own
snapshot cannot: private bytes say how much the process holds, this says which
heap holds it. The engine prints the dump into the game's own log rather than
returning it, so this takes the log's length first, makes the call, and reads
only what appeared after.

Run it beside a driver snapshot -- before a long run and again after the wall,
where "could not allocate" starts -- and the two readings name the heap that
grew.

    python Tools/runtime/heap-snapshot.py
    python Tools/runtime/heap-snapshot.py --port 18701 --detail
"""

import argparse
import os
import re
import socket
import struct
import sys
import time

DEFAULT_LOG = r"D:\Games\ATRI-TestVersion\game_debug_log.txt"


def send_tjs(port, code):
    """Runs TJS in the game through the plugin's TCP port."""
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


def read_tail(path, start):
    with open(path, encoding="utf-8", errors="replace") as handle:
        handle.seek(start)
        return handle.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=18700,
                        help="the plugin's TCP port (18701 for the copy)")
    parser.add_argument("--log", default=DEFAULT_LOG,
                        help="the game's own log, where the dump is printed")
    parser.add_argument("--detail", action="store_true",
                        help="list every heap, not only the largest few")
    options = parser.parse_args()

    if not os.path.exists(options.log):
        sys.exit(f"no game log at {options.log}")
    before = os.path.getsize(options.log)

    try:
        send_tjs(options.port, "(function(){ System.dumpHeap(1); return 1; })()")
    except OSError as error:
        sys.exit(f"could not reach the game on port {options.port}: {error}")

    # The dump is written while the call returns, but the log is buffered and
    # flushed on a timer, so the lines arrive on disk seconds later -- and the
    # summary line later still. Waited for rather than slept on: a fixed pause
    # was either too short to see anything or longer than it needed to be.
    # Waited for rather than slept on, and waited for the writing to stop
    # rather than for the summary line: the per-heap detail lands within
    # seconds, while "The process has N heaps" can trail it by another forty --
    # both are at the mercy of the log's flush timer.
    fresh = ""
    settled = 0
    deadline = time.time() + 60.0
    while time.time() < deadline:
        time.sleep(1.0)
        grown = read_tail(options.log, before)
        if grown == fresh and "type:" in fresh:
            settled += 1
            if settled >= 3:
                break
        else:
            settled = 0
        fresh = grown

    heaps = []
    current = None
    for line in fresh.splitlines():
        text = line.split(" ", 1)[-1] if line[:2].isdigit() else line
        heap = re.match(r"#(\d+) type: (\S+)", text.strip())
        if heap:
            current = {"number": int(heap.group(1)), "type": heap.group(2),
                       "allocated": 0, "blocks": 0}
            heaps.append(current)
            continue
        allocated = re.match(r"Allocated: (\d+), size: (\d+)", text.strip())
        if allocated and current is not None:
            current["blocks"] = int(allocated.group(1))
            current["allocated"] = int(allocated.group(2))

    if not heaps:
        sys.exit("the dump did not appear in the log -- wrong log file, or the "
                 "game did not answer")

    total = sum(h["allocated"] for h in heaps)
    print(f"heaps={len(heaps)} allocated={total / (1024 * 1024):.1f}MB")
    shown = heaps if options.detail else sorted(
        heaps, key=lambda h: -h["allocated"])[:3]
    for heap in shown:
        print(f"  #{heap['number']:<3} {heap['type']:<6} "
              f"{heap['allocated'] / (1024 * 1024):8.1f}MB in "
              f"{heap['blocks']} block(s)")


if __name__ == "__main__":
    main()
