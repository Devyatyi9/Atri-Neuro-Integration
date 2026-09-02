"""Load the plugin's TJS path from outside threads, the way real callers do.

Actions arrive on the Neuro polling thread and console commands on their own;
both ask the game questions through ExecuteTJSSync. Normally that is sporadic.
This makes it constant, which is what turns a rare race into a reproducible one.

It found two defects the day it was written: the log file being closed under a
different mutex than the one guarding writes to it, and engine string objects
being built and destroyed off the game's main thread. Before those were fixed
the game died within a minute at 9-28 calls a second; after, it stays up at 150.

Needs the game running with the plugin loaded. Read-only: every expression here
asks and changes nothing, so a run leaves no trace in the save data.

    py tjs-load.py [seconds] [threads]

Run it against the story with skip mode on, not against the menus: skip advances
the text continuously and keeps raising the page-end glyph, which is the churn
the original crash needed. The menus are the easy case -- the same run reaches
155 calls a second there and only 35 under skip, because the main thread is busy
and synchronous calls wait their turn.

Watch afterwards: the process should still be alive and answering, plugin_log
should carry no "[Fatal]" line, and `reentry=` in the periodic line should stay
at zero. Errors reported as WinError 10048 are this script running out of
ephemeral ports, not the game failing.
"""

import socket
import struct
import sys
import threading
import time

PORT = 18700
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
THREADS = int(sys.argv[2]) if len(sys.argv) > 2 else 3

# The shape of what the probes actually ask: screen state, control lookups,
# layer geometry. Nothing here changes anything -- the point is the traffic and
# the moment it lands, not the effect.
# Every one of these is wrapped the way the plugin's own probes are: an
# expression that throws is not a fair test of the traffic, because the real
# probes never let one escape. The unguarded version of the third payload threw
# during a screen transition and the run that followed died -- which says
# nothing about traffic until the throwing is removed.
PAYLOADS = [
    "(function(){ try { return typeof SystemHook != 'undefined'"
    " ? '' + SystemHook.currentState : ''; } catch (e) { return ''; } })()",
    "(function(){ try { if (typeof kag.current != 'Object') return '';"
    " var n = kag.current.names; var c = 0;"
    " for (var i = 0; i < 10; i++) { if (typeof n['item0' + i] == 'Object') c++; }"
    " return '' + c; } catch (e) { return ''; } })()",
    "(function(){ try { var g = kag.fore.base.pageBreak;"
    " if (typeof g != 'Object' || !g) return '';"
    " return '' + g.imageTop + ',' + g.imageHeight + ',' + (g.visible ? 1 : 0); }"
    " catch (e) { return ''; } })()",
    "(function(){ try { return '' + kag.conductor.curStorage + ':'"
    " + kag.conductor.status; } catch (e) { return ''; } })()",
    "(function(){ try { if (typeof kag.QuickMenu != 'Object') return '';"
    " return '' + (kag.QuickMenu._dropdown ? 1 : 0); } catch (e) { return ''; } })()",
]

stop_at = time.monotonic() + SECONDS
counts = [0] * THREADS
errors = [0] * THREADS
last_error = [""] * THREADS


def call(payload):
    with socket.create_connection(("127.0.0.1", PORT), timeout=10) as sock:
        data = payload.encode("utf-8")
        sock.sendall(struct.pack("<I", len(data)) + data)
        header = sock.recv(4)
        if len(header) < 4:
            raise IOError("short header")
        size = struct.unpack("<I", header)[0]
        body = b""
        while len(body) < size:
            chunk = sock.recv(size - len(body))
            if not chunk:
                raise IOError("short body")
            body += chunk
        return body.decode("utf-8", "replace")


def worker(index):
    pick = index
    while time.monotonic() < stop_at:
        payload = PAYLOADS[pick % len(PAYLOADS)]
        pick += 1
        try:
            call(payload)
            counts[index] += 1
        except Exception as error:  # noqa: BLE001 - the failure itself is data
            errors[index] += 1
            last_error[index] = f"{type(error).__name__}: {error}"
            time.sleep(0.05)


started = time.monotonic()
threads = [threading.Thread(target=worker, args=(i,)) for i in range(THREADS)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
elapsed = time.monotonic() - started

total = sum(counts)
print(f"threads={THREADS} elapsed={elapsed:.1f}s calls={total} "
      f"rate={total / elapsed:.0f}/s errors={sum(errors)}")
for index in range(THREADS):
    if errors[index]:
        print(f"  thread {index}: {errors[index]} errors, last {last_error[index]}")
