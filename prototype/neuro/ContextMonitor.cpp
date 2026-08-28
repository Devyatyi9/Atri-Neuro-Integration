#include "ContextMonitor.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include "ActionRouter.h"
#include "ContextPayload.h"
#include "Logging.h"
#include "NeuroClient.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"
#include "ThreadGuard.h"
#include "UtfConversion.h"

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

static std::atomic<bool> g_MonitorRunning(false);
static std::thread g_MonitorThread;

static void ContextMonitorThread() {
	Log("[Monitor] Context monitor started (poll interval 200ms, via "
	    "context_monitor.tjs)\n");

	// The whole loop is a single point of failure: capability sync, exit-hook
	// upkeep and the context stream all hang off it, so if it stops iterating
	// everything downstream silently stops with it (observed 2026-08-19 -- a
	// stalled loop looked exactly like "capability switches are slow"). These
	// counters make a stall visible and say which call it stopped in, instead of
	// leaving the loop to fail without a trace.
	unsigned long long iteration = 0;
	while (g_MonitorRunning.load()) {
		Sleep(200);

		if (!g_MonitorRunning.load())
			break;

		ULONGLONG const iterationStarted = GetTickCount64();
		std::string result;
		// Run the external TJS script.
		// neuroExecFile returns the result of the final expression.
		// silent=true: do not log to plugin_log.txt.
		bool ok = ExecuteTJSSync(
		    L"neuroExecFile(\"neuro_scripts/context_monitor.tjs\", 1)", &result,
		    5000, TJSExecMode::Expression);
		ULONGLONG const afterScript = GetTickCount64();
		// Capability and exit-hook upkeep must not depend on the context script
		// succeeding: they are separate concerns with their own probes, and
		// skipping them on a dropped poll delayed action registration for as
		// long as the drops lasted (measured at 15s on 2026-08-19).
		RefreshPrototypeActionState();
		RetryInstallExitHookIfReady();
		ULONGLONG const afterUpkeep = GetTickCount64();

		if (afterUpkeep - iterationStarted > 1000) {
			char buf[192];
			_snprintf_s(buf, sizeof(buf),
			            "[Monitor] Slow iteration %llu: script=%llums upkeep=%llums "
			            "ok=%d\n",
			            iteration, afterScript - iterationStarted,
			            afterUpkeep - afterScript, ok ? 1 : 0);
			Log(buf);
		}
		// Roughly every 20s at the 200ms interval; silence means a stall.
		if (++iteration % 100 == 0) {
			char buf[128];
			_snprintf_s(buf, sizeof(buf), "[Monitor] Alive, iteration %llu\n",
			            iteration);
			Log(buf);
		}

		if (!ok)
			continue;

		if (result.empty())
			continue;

		// Write changes to neuro_tag_log.txt.
		std::string const &utf8Result = result;
		{
			LogTagFile(utf8Result.c_str());

			// Everything above stays research output; only the curated semantic
			// events become context for Neuro, coalesced into one message per
			// tick so a fast click-through is not a burst of messages.
			ContextPayload const payload = BuildContextPayload(utf8Result);
			if (!payload.Empty()) {
				NeuroSendContext(payload.message.c_str(), payload.silent);
			}
		}
	}

	Log("[Monitor] Context monitor stopped\n");
}

bool StartContextMonitor() {
	if (g_MonitorRunning.load())
		return true;
	if (g_MonitorThread.joinable())
		g_MonitorThread.join();
	g_MonitorRunning.store(true);
	g_MonitorThread = std::thread(
	    [] { RunThreadGuarded("context monitor", ContextMonitorThread); });
	return true;
}

void StopContextMonitor() {
	if (!g_MonitorRunning.load())
		return;
	g_MonitorRunning.store(false);
	if (g_MonitorThread.joinable())
		g_MonitorThread.join();
}

bool IsContextMonitorRunning() {
	return g_MonitorRunning.load();
}
