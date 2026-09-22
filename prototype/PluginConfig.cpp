#include "PluginConfig.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "tooling/NeuroTCPServer.h"

namespace {

std::wstring g_ConfigSource;
bool g_ConfigCreated = false;

// What a config this run writes for itself says. Comments with ';' rather than
// '#', because that is the one an ini highlighter recognises everywhere; the
// parser takes both.
wchar_t const *const kDefaultConfigText =
    L"; Neuro integration plugin settings.\n"
    L";\n"
    L"; Written by the plugin on a start that found no config of its own. "
    L"Your\n"
    L"; edits are safe: an existing file is never rewritten.\n"
    L";\n"
    L"; Read once, at startup. Changing it while the game runs has no effect: "
    L"a\n"
    L"; port cannot move under a listening socket, nor a path under an open "
    L"file.\n"
    L"\n"
    L"; Where Neuro is. Used ahead of the NEURO_WS_URL environment variable:\n"
    L"; a file that names an address is a decision, a variable left in a shell "
    L"is\n"
    L"; usually an accident. Left empty, as written here, the variable "
    L"decides\n"
    L"; and the tooling that starts the game with an address keeps working.\n"
    L";   ws_url = ws://localhost:8000     (ws:// may be left out)\n"
    L"ws_url =\n"
    L"\n"
    L"; The plugin's own TCP port, for the tooling that talks to it. "
    L"Machine-wide,\n"
    L"; so two instances must not share it -- and sharing it does not fail\n"
    L"; cleanly: the socket is opened with SO_REUSEADDR, so the second bind\n"
    L"; succeeds and connections go to whichever socket the stack picks.\n"
    L"tcp_port = 18700\n"
    L"\n"
    L"; Whether the plugin writes its logs at all.\n"
    L"logs = on\n"
    L"\n"
    L"; Where the logs go. Empty means the game's own folder. Created if "
    L"missing.\n"
    L"; Give each instance its own directory: nothing marks a log file as\n"
    L"; belonging to one, and two instances in one directory append into the "
    L"same\n"
    L"; file.\n"
    L"log_dir =\n"
    L"\n"
    L"; One line per action in the plugin log, saying where its time went:\n"
    L"; the handler's own work, the part of it spent waiting on the game's "
    L"main\n"
    L"; thread, and the wait before the result goes out. For measuring, not "
    L"for\n"
    L"; playing -- a route sends thousands of actions.\n"
    L"log_action_timing = off\n"
    L"\n"
    L"; Stamp every line of the tag log with the wall clock. For lining the "
    L"log up\n"
    L"; against another process's record of the same run.\n"
    L"log_timestamps = off\n"
    L"\n"
    L"; The one-time settings the plugin applies on a first run.\n"
    L";   auto  -- once, on a profile that has not had them (the default)\n"
    L";   apply -- on every start, whatever the profile says\n"
    L";   skip  -- never; the game's settings are left as the player has them\n"
    L"first_run = auto\n"
    L"\n"
    L"; Actions Neuro is never offered, by name, separated by commas.\n"
    L";   disabled_actions = skip_movie\n"
    L"disabled_actions =\n"
    L"\n"
    L"; The red dot that blinks in a corner of the game while the link to "
    L"Neuro\n"
    L"; is down.\n"
    L"link_indicator = on\n";

bool ReadSwitch(std::wstring const &value, bool &out) {
	// Every spelling a person reasonably writes for a switch. Anything else
	// leaves the default rather than guessing.
	if (value == L"on" || value == L"true" || value == L"yes" || value == L"1") {
		out = true;
		return true;
	}
	if (value == L"off" || value == L"false" || value == L"no" || value == L"0") {
		out = false;
		return true;
	}
	return false;
}

std::string Narrow(std::wstring const &text) {
	if (text.empty()) {
		return std::string();
	}
	int const size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
	                                     static_cast<int>(text.size()), nullptr,
	                                     0, nullptr, nullptr);
	if (size <= 0) {
		return std::string();
	}
	std::string narrow(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    &narrow[0], size, nullptr, nullptr);
	return narrow;
}

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
	std::wstring const prefix =
	    directory.empty() ? std::wstring() : directory + L"\\";

	// The exe-named file first: it is the one a second copy uses, and a shared
	// file must not take it over. Then the plain name this version writes, then
	// the names earlier versions used, so an installation that has one keeps
	// working.
	wchar_t const *const kNames[] = {L".neuro.ini", L".neuro.cfg"};
	for (auto const *suffix : kNames) {
		std::wstring const named = prefix + StemOf(exePath) + suffix;
		if (Exists(named)) {
			return named;
		}
	}
	wchar_t const *const kShared[] = {L"neuro.ini", L"neuro_plugin.cfg"};
	for (auto const *name : kShared) {
		std::wstring const shared = prefix + name;
		if (Exists(shared)) {
			return shared;
		}
	}
	return std::wstring();
}

// Where a config this run writes for itself goes: beside the exe, under the
// plain name. Not the exe-named one -- that name means "this copy only", and a
// file created without being asked for should be the one every copy reads.
std::wstring DefaultConfigPath() {
	wchar_t exePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
		return std::wstring();
	}
	std::wstring const directory = DirectoryOf(exePath);
	return (directory.empty() ? std::wstring() : directory + L"\\") +
	       L"neuro.ini";
}

// Writes the defaults out, so the settings can be found and edited instead of
// being read about. Only ever called when no config was found, so it cannot
// overwrite one; a failure is silent here and reported later, because logging
// is configured by the very file this is writing.
bool WriteDefaultConfig(std::wstring const &path) {
	if (path.empty() || Exists(path)) {
		return false;
	}
	FILE *file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"wt, ccs=UTF-8") != 0 || !file) {
		return false;
	}
	bool const written = fputws(kDefaultConfigText, file) >= 0;
	fclose(file);
	return written;
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
	if (key == L"log_timestamps") {
		ReadSwitch(value, config.LogTimestamps);
		return;
	}
	if (key == L"log_action_timing") {
		ReadSwitch(value, config.LogActionTiming);
		return;
	}
	if (key == L"logs") {
		ReadSwitch(value, config.Logs);
		return;
	}
	if (key == L"link_indicator") {
		ReadSwitch(value, config.LinkIndicator);
		return;
	}
	if (key == L"ws_url") {
		config.WsUrl = EnsureWsScheme(Narrow(value));
		return;
	}
	if (key == L"disabled_actions") {
		config.DisabledActions = Narrow(value);
		return;
	}
	if (key == L"first_run") {
		// An unreadable value leaves the default, the same as an unusable port:
		// applying settings nobody asked for is worse than ignoring a typo.
		if (value == L"auto") {
			config.FirstRun = FirstRunMode::Auto;
		} else if (value == L"apply") {
			config.FirstRun = FirstRunMode::Apply;
		} else if (value == L"skip") {
			config.FirstRun = FirstRunMode::Skip;
		}
		return;
	}
	// Unknown keys are ignored on purpose: a config written for a later version
	// should not stop this one from starting.
}

// The command line as a list of arguments, quotes honoured: a path with a
// space in it is one argument, and the quotes are not part of it.
std::vector<std::wstring> SplitCommandLine(std::wstring const &commandLine) {
	std::vector<std::wstring> arguments;
	std::wstring current;
	bool quoted = false;
	bool started = false;
	for (wchar_t const character : commandLine) {
		if (character == L'"') {
			quoted = !quoted;
			started = true;
			continue;
		}
		if (!quoted && (character == L' ' || character == L'\t')) {
			if (started) {
				arguments.push_back(current);
				current.clear();
				started = false;
			}
			continue;
		}
		current += character;
		started = true;
	}
	if (started) {
		arguments.push_back(current);
	}
	return arguments;
}

// An argument's key and value, or false when it is not one of ours. The key
// comes back spelled as the file spells it, so one table of names serves both.
bool ReadArgument(std::wstring const &argument,
                  std::wstring &key,
                  std::wstring &value) {
	std::wstring rest = argument;
	if (rest.compare(0, 2, L"--") == 0) {
		rest.erase(0, 2);
	} else if (!rest.empty() && (rest[0] == L'-' || rest[0] == L'/')) {
		rest.erase(0, 1);
	} else {
		return false;
	}
	if (rest.compare(0, 6, L"neuro-") != 0) {
		return false;
	}
	rest.erase(0, 6);
	std::size_t const equals = rest.find(L'=');
	if (equals == std::wstring::npos) {
		return false;
	}
	key = Trim(rest.substr(0, equals));
	value = Trim(rest.substr(equals + 1));
	// A command line reads better with hyphens; the file has underscores.
	for (wchar_t &character : key) {
		if (character == L'-') {
			character = L'_';
		}
	}
	return !key.empty();
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
	std::wstring const commandLine = GetCommandLineW();
	// A file named on the command line is used as named, even when it is not
	// there yet: asking for a config by path is also a way of asking for one.
	// Otherwise the usual search, and failing that the plain name beside the
	// exe, so an installation without a config ends up with one to edit.
	std::wstring path = CommandLineConfigPath(commandLine);
	if (path.empty()) {
		path = FindConfigFile();
	}
	if (path.empty()) {
		path = DefaultConfigPath();
	}
	if (!path.empty() && WriteDefaultConfig(path)) {
		g_ConfigCreated = true;
	}
	PluginConfig config = ParsePluginConfig(std::wstring());
	if (!path.empty()) {
		std::wstring const text = ReadWholeFile(path);
		if (!text.empty()) {
			g_ConfigSource = path;
			config = ParsePluginConfig(text);
		}
	}
	// Last, because an argument is typed for this one launch.
	ApplyCommandLine(config, commandLine);
	return config;
}

}  // namespace

PluginConfig ParsePluginConfig(std::wstring const &text) {
	PluginConfig config;
	config.TcpPort = NEURO_TCP_PORT;
	config.LogTimestamps = false;
	config.LogActionTiming = false;
	config.Logs = true;
	config.FirstRun = FirstRunMode::Auto;
	config.LinkIndicator = true;

	std::size_t start = 0;
	while (start <= text.size()) {
		std::size_t const end = text.find(L'\n', start);
		std::wstring const line = Trim(text.substr(
		    start, end == std::wstring::npos ? std::wstring::npos : end - start));
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

void ApplyCommandLine(PluginConfig &config, std::wstring const &commandLine) {
	for (std::wstring const &argument : SplitCommandLine(commandLine)) {
		std::wstring key;
		std::wstring value;
		if (!ReadArgument(argument, key, value)) {
			continue;
		}
		// Which file to read is not a setting in the file, so it is handled
		// where the file is chosen and ignored here.
		if (key == L"config") {
			continue;
		}
		ApplySetting(config, key, value);
	}
}

std::wstring CommandLineConfigPath(std::wstring const &commandLine) {
	std::wstring path;
	for (std::wstring const &argument : SplitCommandLine(commandLine)) {
		std::wstring key;
		std::wstring value;
		if (ReadArgument(argument, key, value) && key == L"config") {
			// Last one wins, the same as a repeated key in the file.
			path = value;
		}
	}
	return path;
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

bool PluginConfigWasCreated() {
	GetPluginConfig();
	return g_ConfigCreated;
}

std::string EnsureWsScheme(std::string const &address) {
	if (address.empty() || address.find("://") != std::string::npos) {
		return address;
	}
	return "ws://" + address;
}

bool PluginConfigActionDisabled(char const *name) {
	if (name == nullptr || *name == 0) {
		return false;
	}
	std::string const &list = GetPluginConfig().DisabledActions;
	std::string const wanted(name);
	std::size_t start = 0;
	while (start <= list.size()) {
		std::size_t const comma = list.find(',', start);
		std::size_t const end = comma == std::string::npos ? list.size() : comma;
		std::size_t first = list.find_first_not_of(" \t", start);
		if (first != std::string::npos && first < end) {
			std::size_t const last = list.find_last_not_of(" \t", end - 1);
			if (list.compare(first, last - first + 1, wanted) == 0) {
				return true;
			}
		}
		if (comma == std::string::npos) {
			break;
		}
		start = comma + 1;
	}
	return false;
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
