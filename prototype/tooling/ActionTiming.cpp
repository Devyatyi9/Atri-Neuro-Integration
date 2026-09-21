#include "ActionTiming.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>

#include "Logging.h"
#include "PluginConfig.h"

namespace {

struct Record {
	bool active = false;
	std::string id;
	std::string name;
	LONGLONG arrived = 0;
	LONGLONG queued = 0;
	DWORD thread = 0;
	int tjsCalls = 0;
	LONGLONG tjsMicros = 0;
	// Where the crossings begin and end, which is what splits the handler into
	// "before the game was asked anything", "asking" and "after".
	LONGLONG firstCall = 0;
	LONGLONG lastCall = 0;
};

std::mutex g_Mutex;
Record g_Current;

LONGLONG Now() {
	LARGE_INTEGER counter = {};
	QueryPerformanceCounter(&counter);
	return counter.QuadPart;
}

double MillisBetween(LONGLONG from, LONGLONG to) {
	static LONGLONG const frequency = [] {
		LARGE_INTEGER value = {};
		QueryPerformanceFrequency(&value);
		return value.QuadPart;
	}();
	if (frequency == 0 || to <= from) {
		return 0.0;
	}
	return static_cast<double>(to - from) * 1000.0 /
	       static_cast<double>(frequency);
}

bool Enabled() {
	return GetPluginConfig().LogActionTiming;
}

}  // namespace

void ActionTimingStart(std::string const &id, std::string const &name) {
	if (!Enabled()) {
		return;
	}
	std::lock_guard<std::mutex> lock(g_Mutex);
	g_Current = Record();
	g_Current.active = true;
	g_Current.id = id;
	g_Current.name = name;
	g_Current.arrived = Now();
	// The handler runs on the thread that dispatched the action, so this is
	// what tells our crossings from the continuous hook's.
	g_Current.thread = GetCurrentThreadId();
}

void ActionTimingResultQueued(std::string const &id) {
	if (!Enabled()) {
		return;
	}
	std::lock_guard<std::mutex> lock(g_Mutex);
	if (!g_Current.active || g_Current.id != id || g_Current.queued != 0) {
		return;
	}
	g_Current.queued = Now();
}

void ActionTimingTjsSpan(long long start, long long end) {
	if (!Enabled()) {
		return;
	}
	std::lock_guard<std::mutex> lock(g_Mutex);
	if (!g_Current.active || g_Current.thread != GetCurrentThreadId()) {
		return;
	}
	g_Current.tjsCalls += 1;
	g_Current.tjsMicros +=
	    static_cast<LONGLONG>(MillisBetween(start, end) * 1000.0);
	if (g_Current.firstCall == 0) {
		g_Current.firstCall = start;
	}
	g_Current.lastCall = end;
}

long long ActionTimingNow() {
	return Now();
}

void ActionTimingResultSent(std::string const &id) {
	if (!Enabled()) {
		return;
	}
	Record record;
	{
		std::lock_guard<std::mutex> lock(g_Mutex);
		if (!g_Current.active || g_Current.id != id) {
			return;
		}
		record = g_Current;
		g_Current.active = false;
	}
	LONGLONG const sent = Now();
	LONGLONG const queued = record.queued != 0 ? record.queued : sent;
	// The handler cut into three: before it asked the game anything, the
	// stretch it spent asking, and what it did after the last answer. An action
	// that never asks has all of it in `pre`.
	LONGLONG const firstCall =
	    record.firstCall != 0 ? record.firstCall : record.arrived;
	LONGLONG const lastCall =
	    record.lastCall != 0 ? record.lastCall : record.arrived;
	char line[320];
	_snprintf_s(line, _TRUNCATE,
	            "[Timing] %s total=%.1fms pre=%.1fms probes=%.1fms/%d "
	            "tjs=%.1fms post=%.1fms send=%.1fms\n",
	            record.name.c_str(), MillisBetween(record.arrived, sent),
	            MillisBetween(record.arrived, firstCall),
	            MillisBetween(firstCall, lastCall), record.tjsCalls,
	            static_cast<double>(record.tjsMicros) / 1000.0,
	            MillisBetween(lastCall, queued), MillisBetween(queued, sent));
	Log(line);
}

ActionTimingCrossing::ActionTimingCrossing() : start_(Now()) { }

ActionTimingCrossing::~ActionTimingCrossing() {
	ActionTimingTjsSpan(start_, Now());
}
