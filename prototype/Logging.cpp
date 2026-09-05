#include "Logging.h"

#include <windows.h>

#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace {
constexpr std::size_t kMaxLogLines = 2000;
constexpr std::size_t kMaxLogCharacters = 200000;

std::mutex g_LogMutex;
std::deque<std::string> g_LogLines;
std::size_t g_LogCharacters = 0;
}  // namespace

void Log(char const *message) {
	if (!message || !*message) {
		return;
	}

	std::lock_guard<std::mutex> lock(g_LogMutex);

	std::string line(message);
	g_LogCharacters += line.size();
	g_LogLines.push_back(std::move(line));
	while (!g_LogLines.empty() && (g_LogLines.size() > kMaxLogLines ||
	                               g_LogCharacters > kMaxLogCharacters)) {
		g_LogCharacters -= g_LogLines.front().size();
		g_LogLines.pop_front();
	}
}

std::wstring GetPluginLog(std::size_t offset, std::size_t count, bool fromEnd) {
	std::lock_guard<std::mutex> lock(g_LogMutex);
	if (count == 0 || offset >= g_LogLines.size()) {
		return {};
	}

	std::size_t first = offset;
	std::size_t last = first + count;
	if (fromEnd) {
		last = g_LogLines.size() - offset;
		first = count < last ? last - count : 0;
	} else if (last > g_LogLines.size()) {
		last = g_LogLines.size();
	}

	std::wstring result;
	for (std::size_t i = first; i < last; ++i) {
		int const size =
		    MultiByteToWideChar(CP_UTF8, 0, g_LogLines[i].data(),
		                        static_cast<int>(g_LogLines[i].size()), nullptr, 0);
		if (size > 0) {
			std::wstring wide(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, g_LogLines[i].data(),
			                    static_cast<int>(g_LogLines[i].size()), &wide[0],
			                    size);
			result += wide;
		}
		result += L'\n';
	}
	return result;
}

void ClearPluginLog() {
	std::lock_guard<std::mutex> lock(g_LogMutex);
	g_LogLines.clear();
	g_LogCharacters = 0;
}
