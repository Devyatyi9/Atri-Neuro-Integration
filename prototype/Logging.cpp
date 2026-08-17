#include "Logging.h"

#include <cstdio>
#include <mutex>

namespace {
FILE *g_LogFile = nullptr;
FILE *g_TagLogFile = nullptr;
std::mutex g_LogMutex;
}  // namespace

void LogToFile(char const *message) {
	std::lock_guard<std::mutex> lock(g_LogMutex);
	if (!g_LogFile) {
		g_LogFile = fopen("plugin_log.txt", "a");
		if (!g_LogFile) {
			return;
		}
	}
	fprintf(g_LogFile, "%s", message);
	fflush(g_LogFile);
}

void CloseLogFile() {
	std::lock_guard<std::mutex> lock(g_LogMutex);
	if (g_LogFile) {
		fclose(g_LogFile);
		g_LogFile = nullptr;
	}
}

void LogTagFile(char const *text) {
	std::lock_guard<std::mutex> lock(g_LogMutex);
	if (!g_TagLogFile) {
		g_TagLogFile = fopen("neuro_tag_log.txt", "a");
		if (!g_TagLogFile) {
			return;
		}
	}
	fprintf(g_TagLogFile, "%s", text);
	fflush(g_TagLogFile);
}

void CloseTagLogFile() {
	std::lock_guard<std::mutex> lock(g_LogMutex);
	if (g_TagLogFile) {
		fclose(g_TagLogFile);
		g_TagLogFile = nullptr;
	}
}
