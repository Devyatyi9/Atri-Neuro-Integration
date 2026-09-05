#include "PluginConfig.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "tooling/NeuroTCPServer.h"

namespace {

std::wstring g_ConfigSource;

std::wstring Trim(std::wstring const &text) {
	std::size_t const first = text.find_first_not_of(L" \t\r\n");
	if (first == std::wstring::npos) {
		return std::wstring();
	}
	std::size_t const last = text.find_last_not_of(L" \t\r\n");
	return text.substr(first, last - first + 1);
}

std::wstring DirectoryOf(std::wstring const &path) {
	std::size_t const slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring StemOf(std::wstring const &path) {
	std::size_t const slash = path.find_last_of(L"\\/");
	std::wstring const file =
	    slash == std::wstring::npos ? path : path.substr(slash + 1);
	std::size_t const dot = file.find_last_of(L'.');
	return dot == std::wstring::npos ? file : file.substr(0, dot);
}

bool Exists(std::wstring const &path) {
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Where to look, most specific first. The exe-named file is what makes a second
// copy work without a launcher: copy the exe, drop a config beside it under the
// copy's own name, and it picks its own port and log directory up by itself.
std::wstring FindConfigFile() {
	wchar_t fromEnvironment[MAX_PATH] = {};
	DWORD const length = GetEnvironmentVariableW(L"NEURO_PLUGIN_CONFIG",
	                                             fromEnvironment, MAX_PATH);
	if (length > 0 && length < MAX_PATH) {
		return fromEnvironment;
	}

	wchar_t exePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
		return std::wstring();
	}
	std::wstring const directory = DirectoryOf(exePath);
	std::wstring const prefix = directory.empty() ? std::wstring() : directory + L"\\";

	std::wstring named = prefix + StemOf(exePath) + L".neuro.cfg";
	if (Exists(named)) {
		return named;
	}
	std::wstring shared = prefix + L"neuro_plugin.cfg";
	if (Exists(shared)) {
		return shared;
	}
	return std::wstring();
}

void ApplySetting(PluginConfig &config,
                  std::wstring const &key,
                  std::wstring const &value) {
	if (key == L"tcp_port") {
		int const port = _wtoi(value.c_str());
		// A port outside this range would fail to bind and leave the plugin
		// unreachable with nothing said about why, so keep the default instead.
		if (port > 0 && port < 65536) {
			config.TcpPort = port;
		}
		return;
	}
	if (key == L"log_dir") {
		config.LogDirectory = value;
		return;
	}
	// Unknown keys are ignored on purpose: a config written for a later version
	// should not stop this one from starting.
}

std::wstring ReadWholeFile(std::wstring const &path) {
	FILE *file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"rt, ccs=UTF-8") != 0 || !file) {
		return std::wstring();
	}
	std::wstring text;
	wchar_t line[512] = {};
	while (fgetws(line, _countof(line), file)) {
		text += line;
	}
	fclose(file);
	// An empty file still counts as read: it means "use the defaults", which is
	// different from "no file was found", and the reported source says which.
	return text.empty() ? std::wstring(L"\n") : text;
}

PluginConfig LoadConfig() {
	std::wstring const path = FindConfigFile();
	if (path.empty()) {
		return ParsePluginConfig(std::wstring());
	}
	std::wstring const text = ReadWholeFile(path);
	if (text.empty()) {
		return ParsePluginConfig(std::wstring());
	}
	g_ConfigSource = path;
	return ParsePluginConfig(text);
}

}  // namespace

PluginConfig ParsePluginConfig(std::wstring const &text) {
	PluginConfig config;
	config.TcpPort = NEURO_TCP_PORT;

	std::size_t start = 0;
	while (start <= text.size()) {
		std::size_t const end = text.find(L'\n', start);
		std::wstring const line =
		    Trim(text.substr(start, end == std::wstring::npos ? std::wstring::npos
		                                                      : end - start));
		if (!line.empty() && line[0] != L'#' && line[0] != L';') {
			std::size_t const equals = line.find(L'=');
			if (equals != std::wstring::npos) {
				ApplySetting(config, Trim(line.substr(0, equals)),
				             Trim(line.substr(equals + 1)));
			}
		}
		if (end == std::wstring::npos) {
			break;
		}
		start = end + 1;
	}
	return config;
}

PluginConfig const &GetPluginConfig() {
	// Loaded on first use rather than at DLL attach: this runs before the
	// logging it configures, so there would be nowhere to report a problem.
	static PluginConfig const config = LoadConfig();
	return config;
}

std::wstring const &GetPluginConfigSource() {
	GetPluginConfig();
	return g_ConfigSource;
}

std::string GetInstanceBanner() {
	wchar_t exePath[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);

	std::wstring wide = L"# instance pid=";
	wide += std::to_wstring(GetCurrentProcessId());
	wide += L" exe=";
	wide += exePath;
	wide += L" port=";
	wide += std::to_wstring(GetPluginConfig().TcpPort);
	wide += L" config=";
	wide += GetPluginConfigSource().empty() ? L"(defaults)"
	                                        : GetPluginConfigSource().c_str();
	// Resolved, not as configured: an empty LogDirectory means the process's
	// current directory, and "logs=" followed by nothing answers no question.
	// Whoever reads this line is holding one instance's log while looking for
	// another's, so the answer has to be a path they can open.
	wide += L" logs=";
	if (GetPluginConfig().LogDirectory.empty()) {
		wchar_t current[MAX_PATH] = {};
		if (GetCurrentDirectoryW(MAX_PATH, current) > 0) {
			wide += current;
		} else {
			wide += L"(current directory)";
		}
	} else {
		wide += GetPluginConfig().LogDirectory;
	}
	wide += L"\n";

	// Converted here rather than through UtfConversion, which would drag that
	// unit into the test target for one line.
	int const size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
	                                     static_cast<int>(wide.size()), nullptr,
	                                     0, nullptr, nullptr);
	if (size <= 0) {
		return std::string();
	}
	std::string narrow(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
	                    &narrow[0], size, nullptr, nullptr);
	return narrow;
}

std::wstring ResolveLogPath(wchar_t const *name) {
	std::wstring const &directory = GetPluginConfig().LogDirectory;
	if (directory.empty()) {
		return name;
	}
	CreateDirectoryW(directory.c_str(), nullptr);
	return directory + L"\\" + name;
}
