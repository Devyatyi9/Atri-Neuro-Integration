#include "GameDebugLog.h"

#include <windows.h>

#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

namespace {
constexpr std::size_t kMaxDebugLogLines = 1000;
constexpr std::size_t kMaxDebugLogCharacters = 100000;

std::mutex g_DebugLogMutex;
// Separate from the ring's lock: writing to disk must not block a reader.
std::mutex g_DebugLogFileMutex;
ULONGLONG g_LastDebugLogFlush = 0;
constexpr ULONGLONG kDebugLogFlushIntervalMs = 200;
std::deque<std::wstring> g_DebugLogLines;
std::size_t g_DebugLogCharacters = 0;
FILE *g_DebugLogFile = nullptr;

void AppendGameDebugLogFile(std::wstring const &line) {
	if (!g_DebugLogFile) {
		g_DebugLogFile = _wfopen(L"game_debug_log.txt", L"ab");
		if (!g_DebugLogFile) {
			return;
		}
	}

	int const size = WideCharToMultiByte(CP_UTF8, 0, line.data(),
	                                     static_cast<int>(line.size()), nullptr,
	                                     0, nullptr, nullptr);
	if (size <= 0) {
		return;
	}

	std::string utf8(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
	                    &utf8[0], size, nullptr, nullptr);
	fwrite(utf8.data(), 1, utf8.size(), g_DebugLogFile);
	fwrite("\r\n", 1, 2, g_DebugLogFile);
	// Flushed on a timer, not per line: this runs on the game's main thread
	// through Debug.addLoggingHandler, and a flush per line turned every logged
	// line into a disk wait for the game itself.
	ULONGLONG const now = GetTickCount64();
	if (now - g_LastDebugLogFlush >= kDebugLogFlushIntervalMs) {
		fflush(g_DebugLogFile);
		g_LastDebugLogFlush = now;
	}
}
}  // namespace

void AppendGameDebugLog(std::wstring const &line) {
	if (line.empty()) {
		return;
	}

	// The file first, under its own lock, so the ring stays readable while the
	// disk is busy; the ring is updated after, under the ring's lock.
	{
		std::lock_guard<std::mutex> fileLock(g_DebugLogFileMutex);
		AppendGameDebugLogFile(line);
	}

	std::lock_guard<std::mutex> lock(g_DebugLogMutex);
	if (line.size() > kMaxDebugLogCharacters) {
		g_DebugLogLines.clear();
		g_DebugLogCharacters = 0;
		g_DebugLogLines.emplace_back(
		    line.substr(line.size() - kMaxDebugLogCharacters));
		g_DebugLogCharacters = g_DebugLogLines.back().size();
		return;
	}

	g_DebugLogLines.push_back(line);
	g_DebugLogCharacters += line.size();
	while (!g_DebugLogLines.empty() &&
	       (g_DebugLogLines.size() > kMaxDebugLogLines ||
	        g_DebugLogCharacters > kMaxDebugLogCharacters)) {
		g_DebugLogCharacters -= g_DebugLogLines.front().size();
		g_DebugLogLines.pop_front();
	}
}

std::wstring GetGameDebugLog(std::size_t offset,
                             std::size_t count,
                             bool fromEnd) {
	std::lock_guard<std::mutex> lock(g_DebugLogMutex);
	if (count == 0 || offset >= g_DebugLogLines.size()) {
		return {};
	}

	std::size_t first = offset;
	std::size_t last = first + count;
	if (fromEnd) {
		last = g_DebugLogLines.size() - offset;
		first = count < last ? last - count : 0;
	} else if (last > g_DebugLogLines.size()) {
		last = g_DebugLogLines.size();
	}
	std::wstring result;
	for (std::size_t i = first; i < last; ++i) {
		result += g_DebugLogLines[i];
		result += L'\n';
	}
	return result;
}

void ClearGameDebugLog() {
	std::lock_guard<std::mutex> lock(g_DebugLogMutex);
	g_DebugLogLines.clear();
	g_DebugLogCharacters = 0;
}

void CloseGameDebugLogFile() {
	std::lock_guard<std::mutex> lock(g_DebugLogMutex);
	if (g_DebugLogFile) {
		fclose(g_DebugLogFile);
		g_DebugLogFile = nullptr;
	}
}
