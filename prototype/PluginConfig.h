#pragma once

#include <string>

// The settings two copies of the game cannot share.
//
// Everything else the plugin does is per-process, but the TCP port and the log
// files are not: the port is one number for the whole machine, and the logs are
// opened by a fixed name in the game's own directory. Run a second copy with
// both left at their defaults and neither instance is usable -- the logs are
// written by two processes at once, and the listening socket is opened with
// SO_REUSEADDR, which on Windows lets the second bind succeed and then hands
// connections to whichever socket the stack picks.
//
// Read once, on first use, and never reread: the port cannot change under a
// listening socket, and a log path cannot change under an open file.
struct PluginConfig {
	int TcpPort;
	// Where the logs are written. Empty means the process's current directory,
	// which is what every earlier version did.
	std::wstring LogDirectory;
};

PluginConfig const &GetPluginConfig();

// Applies the settings in `text` on top of the defaults. Separate from reading
// the file so the parsing can be tested without one.
PluginConfig ParsePluginConfig(std::wstring const &text);

// The file the settings came from, for the plugin to report once logging is up.
// Empty when no file was found and the defaults are in use.
std::wstring const &GetPluginConfigSource();

// The full path to a log file, honouring LogDirectory. Creates the directory if
// it is named and missing.
std::wstring ResolveLogPath(wchar_t const *name);

// One line naming this process, written at the top of every log it opens.
//
// Nothing marks a log file as belonging to an instance: the files are told
// apart by their path and nothing else. Point two instances at one directory
// and the second finds the first's log already there, fails to move it aside
// because it is open, and appends into it -- silently. This line is what makes
// that readable afterwards, and it is why the first thing in a log is which
// process, which exe and which config produced it.
std::string GetInstanceBanner();
