#include "ContextMonitor.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

void LogToFile(char const *message);
bool ExecuteTJSSync(wchar_t const *code, tTJSVariant *result, DWORD timeoutMs);

// External declaration: logs a string directly to neuro_tag_log.txt
void LogTagFile(char const *text);

static std::atomic<bool> g_MonitorRunning(false);
static std::thread g_MonitorThread;

static void ContextMonitorThread() {
	LogToFile(
	    "[Monitor] Context monitor started (poll interval 200ms, via "
	    "context_monitor.tjs)\n");

	while (g_MonitorRunning.load()) {
		Sleep(200);

		if (!g_MonitorRunning.load())
			break;

		tTJSVariant result;
		// Вызываем внешний TJS-скрипт.
		// neuroExecFile возвращает результат последнего выражения.
		// silent=true — не логировать в plugin_log.txt
		bool ok = ExecuteTJSSync(
		    L"neuroExecFile(\"neuro_scripts/context_monitor.tjs\", 1)", &result,
		    5000);
		if (!ok)
			continue;

		ttstr resultStr(result);
		if (resultStr.IsEmpty())
			continue;

		// Если есть изменения — пишем в neuro_tag_log.txt
		int utf8Len = WideCharToMultiByte(CP_UTF8, 0, resultStr.c_str(), -1, NULL,
		                                  0, NULL, NULL);
		if (utf8Len > 0) {
			std::string utf8Result(utf8Len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, resultStr.c_str(), -1, &utf8Result[0],
			                    utf8Len, NULL, NULL);
			// Убираем trailing null
			while (!utf8Result.empty() && utf8Result.back() == '\0')
				utf8Result.pop_back();
			if (!utf8Result.empty()) {
				LogTagFile(utf8Result.c_str());
			}
		}
	}

	LogToFile("[Monitor] Context monitor stopped\n");
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
