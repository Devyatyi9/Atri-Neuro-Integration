#include "Logging.h"

#include "PluginConfig.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace {
constexpr std::size_t kMaxLogLines = 2000;
constexpr std::size_t kMaxLogCharacters = 200000;

FILE *g_LogFile = nullptr;
FILE *g_TagLogFile = nullptr;
std::mutex g_LogMutex;
// Separate from the ring's lock so writing to disk never blocks a reader.
std::mutex g_LogFileMutex;
std::mutex g_TagLogFileMutex;
ULONGLONG g_LastLogFlush = 0;
ULONGLONG g_LastTagLogFlush = 0;
constexpr ULONGLONG kLogFlushIntervalMs = 200;
std::deque<std::string> g_LogLines;
std::size_t g_LogCharacters = 0;

// Local time with the offset spelled out, so the stamp is both readable at a
// glance and unambiguous when a tool lines this log up against a record kept by
// another process. Milliseconds matter: the monitor polls five times a second.
std::string FormatTimestamp() {
	SYSTEMTIME local = {};
	GetLocalTime(&local);

	TIME_ZONE_INFORMATION zone = {};
	DWORD const kind = GetTimeZoneInformation(&zone);
	LONG bias = zone.Bias;
	if (kind == TIME_ZONE_ID_STANDARD) {
		bias += zone.StandardBias;
	} else if (kind == TIME_ZONE_ID_DAYLIGHT) {
		bias += zone.DaylightBias;
	}
	// Windows reports the bias as minutes to ADD to local time to reach UTC,
	// which is the opposite sign from the one an ISO offset carries.
	LONG const offset = -bias;
	char const sign = offset < 0 ? '-' : '+';
	LONG const absolute = offset < 0 ? -offset : offset;

	char buffer[64] = {};
	_snprintf_s(buffer, _countof(buffer), _TRUNCATE,
	            "%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02ld:%02ld", local.wYear,
	            local.wMonth, local.wDay, local.wHour, local.wMinute,
	            local.wSecond, local.wMilliseconds, sign, absolute / 60,
	            absolute % 60);
	return buffer;
}

// Writes `text` with every line prefixed by one stamp. All the lines of a call
// share it on purpose: they come from a single poll, and pretending they were
// observed at different moments would invent an order the game never had.
void WriteStamped(FILE *file, char const *text) {
	std::string const stamp = FormatTimestamp();
	char const *line = text;
	while (*line) {
		char const *end = strchr(line, '\n');
		if (!end) {
			fprintf(file, "%s %s", stamp.c_str(), line);
			return;
		}
		fprintf(file, "%s %.*s\n", stamp.c_str(), static_cast<int>(end - line),
		        line);
		line = end + 1;
	}
}
}  // namespace

void ArchiveExistingLog(wchar_t const *path) {
	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) {
		return;
	}
	// An empty file is not worth a slot in the archive.
	if (attributes.nFileSizeLow == 0 && attributes.nFileSizeHigh == 0) {
		return;
	}

	// The archive sits beside the log, wherever the log was configured to be,
	// so two instances writing to different directories keep separate histories.
	std::wstring const full(path);
	std::size_t const slash = full.find_last_of(L"\\/");
	std::wstring const directory =
	    slash == std::wstring::npos ? std::wstring() : full.substr(0, slash + 1);
	std::wstring const archive = directory + L"Logs";
	CreateDirectoryW(archive.c_str(), nullptr);

	// Named after the last write rather than the move, so the name says when the
	// run it belongs to ended.
	FILETIME local = {};
	SYSTEMTIME time = {};
	if (!FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local) ||
	    !FileTimeToSystemTime(&local, &time)) {
		return;
	}

	std::wstring const file =
	    slash == std::wstring::npos ? full : full.substr(slash + 1);
	std::size_t const dot = file.find_last_of(L'.');
	std::wstring const stem =
	    dot == std::wstring::npos ? file : file.substr(0, dot);
	std::wstring const extension =
	    dot == std::wstring::npos ? L"" : file.substr(dot);

	wchar_t stamp[32] = {};
	swprintf_s(stamp, L"-%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth,
	           time.wDay, time.wHour, time.wMinute, time.wSecond);

	// Two runs can end inside the same second, so the plain name is only the
	// first candidate. Never replaces: an archived log is not worth losing.
	for (int attempt = 0; attempt < 100; ++attempt) {
		std::wstring target = archive;
		target += L"\\";
		target += stem;
		target += stamp;
		if (attempt > 0) {
			wchar_t suffix[16] = {};
			swprintf_s(suffix, L"-%d", attempt);
			target += suffix;
		}
		target += extension;
		if (MoveFileExW(path, target.c_str(), 0)) {
			return;
		}
	}
}

void Log(char const *message) {
	// With the logs switched off only the files go: the ring in memory stays,
	// because the console and the TCP client read the recent lines from it and
	// a run without files is still worth watching live.
	bool const toFile = GetPluginConfig().Logs;
	// The file is written outside the ring's lock, and flushed on a timer
	// rather than per line. Both mattered: this runs on the game's main thread
	// for every synchronous TJS call, and a per-line fflush there put the game
	// into disk waits -- a flood of probes hung it outright, with the main
	// thread parked in this path. Holding the ring's lock across the write would
	// also block anyone reading the log back.
	if (toFile) {
		std::lock_guard<std::mutex> fileLock(g_LogFileMutex);
		if (!g_LogFile) {
			// First open in this process, so the previous run's file goes aside.
			std::wstring const path = ResolveLogPath(L"plugin_log.txt");
			ArchiveExistingLog(path.c_str());
			g_LogFile = _wfopen(path.c_str(), L"a");
			if (g_LogFile) {
				std::string const banner = GetInstanceBanner();
				fprintf(g_LogFile, "%s", banner.c_str());
			}
		}
		if (g_LogFile) {
			fprintf(g_LogFile, "%s", message);
			// A hard crash can still lose what is inside this window, which is
			// the price for not stalling the thread that produces the lines.
			ULONGLONG const now = GetTickCount64();
			if (now - g_LastLogFlush >= kLogFlushIntervalMs) {
				fflush(g_LogFile);
				g_LastLogFlush = now;
			}
		}
	}

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

void FlushLogFileIfFree() {
	std::unique_lock<std::mutex> fileLock(g_LogFileMutex, std::try_to_lock);
	if (fileLock.owns_lock() && g_LogFile) {
		fflush(g_LogFile);
	}
}

void CloseLogFile() {
	// Two locks, one after the other, because this touches both halves and Log
	// keeps them apart: the file under its own lock, the ring under the ring's.
	//
	// Closing the file under the ring's lock is a use-after-free: another thread
	// can be inside fprintf on the FILE* while this closes it.
	//
	// Never held together, and in this order only: Log takes the file lock and
	// releases it before taking the ring's, so nothing nests either way.
	{
		std::lock_guard<std::mutex> fileLock(g_LogFileMutex);
		if (g_LogFile) {
			fclose(g_LogFile);
			g_LogFile = nullptr;
		}
	}

	std::lock_guard<std::mutex> lock(g_LogMutex);
	g_LogLines.clear();
	g_LogCharacters = 0;
}

std::wstring GetPluginLogFile(std::size_t offset,
                              std::size_t count,
                              bool fromEnd) {
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

void LogTagFile(char const *text) {
	if (!GetPluginConfig().Logs) {
		return;
	}
	// The hottest of the three log paths: it runs from the onTag hook, on the
	// game's main thread, around fourteen times per line of dialogue. Hence its
	// own lock and no flush per call -- either would put the game on the disk.
	std::lock_guard<std::mutex> lock(g_TagLogFileMutex);
	if (!g_TagLogFile) {
		std::wstring const path = ResolveLogPath(L"neuro_tag_log.txt");
		ArchiveExistingLog(path.c_str());
		g_TagLogFile = _wfopen(path.c_str(), L"a");
		if (!g_TagLogFile) {
			return;
		}
		std::string const banner = GetInstanceBanner();
		fprintf(g_TagLogFile, "%s", banner.c_str());
	}
	if (GetPluginConfig().LogTimestamps) {
		WriteStamped(g_TagLogFile, text);
	} else {
		fprintf(g_TagLogFile, "%s", text);
	}
	ULONGLONG const now = GetTickCount64();
	if (now - g_LastTagLogFlush >= kLogFlushIntervalMs) {
		fflush(g_TagLogFile);
		g_LastTagLogFlush = now;
	}
}

void CloseTagLogFile() {
	// The file's own lock, matching LogTagFile: the ring's mutex no longer
	// guards it. fclose flushes what the throttle has not written yet.
	std::lock_guard<std::mutex> lock(g_TagLogFileMutex);
	if (g_TagLogFile) {
		fclose(g_TagLogFile);
		g_TagLogFile = nullptr;
	}
}