#include "ContextMonitor.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include "ActionRouter.h"
#include "Logging.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"
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

	while (g_MonitorRunning.load()) {
		Sleep(200);

		if (!g_MonitorRunning.load())
			break;

		tTJSVariant result;
		// Run the external TJS script.
		// neuroExecFile returns the result of the final expression.
		// silent=true: do not log to plugin_log.txt.
		bool ok = ExecuteTJSSync(
		    L"neuroExecFile(\"neuro_scripts/context_monitor.tjs\", 1)", &result,
		    5000, TJSExecMode::Expression);
		// Capability and exit-hook upkeep must not depend on the context script
		// succeeding: they are separate concerns with their own probes, and
		// skipping them on a dropped poll delayed action registration for as
		// long as the drops lasted (measured at 15s on 2026-08-19).
		RefreshPrototypeActionState();
		RetryInstallExitHookIfReady();

		if (!ok)
			continue;

		ttstr resultStr(result);
		if (resultStr.IsEmpty())
			continue;

		// Write changes to neuro_tag_log.txt.
		std::string utf8Result;
		if (Utf16ToUtf8(resultStr.c_str(), resultStr.GetLen(), utf8Result) &&
		    !utf8Result.empty()) {
			LogTagFile(utf8Result.c_str());
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
	g_MonitorThread = std::thread(ContextMonitorThread);
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
