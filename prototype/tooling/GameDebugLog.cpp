#include "GameDebugLog.h"

#include <deque>
#include <mutex>
#include <utility>

namespace {
constexpr std::size_t kMaxDebugLogLines = 1000;
constexpr std::size_t kMaxDebugLogCharacters = 100000;

std::mutex g_DebugLogMutex;
std::deque<std::wstring> g_DebugLogLines;
std::size_t g_DebugLogCharacters = 0;
}  // namespace

void AppendGameDebugLog(std::wstring const &line) {
	if (line.empty()) {
		return;
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
