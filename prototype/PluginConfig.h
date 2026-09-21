#pragma once

#include <string>

// The plugin's settings, as a file beside the game's exe.
//
// Some of them are the ones two copies of the game cannot share: the TCP port
// is one number for the whole machine, and the logs are opened by a fixed name
// in the game's own directory. Run a second copy with both left at their
// defaults and neither instance is usable -- the logs are written by two
// processes at once, and the listening socket is opened with SO_REUSEADDR,
// which on Windows lets the second bind succeed and then hands connections to
// whichever socket the stack picks. The rest are choices a player makes: where
// Neuro is, what the plugin may do, what it writes down.
//
// Read once, on first use, and never reread: the port cannot change under a
// listening socket, and a log path cannot change under an open file.
// What to do about the one-time settings the plugin applies on a first run.
enum class FirstRunMode {
	// Apply them once, on a profile that has not seen them. What the plugin has
	// always done, and what the flag in the game's own system variables decides.
	Auto,
	// Apply them on every start, whatever the flag says. For getting them back
	// after the game's settings screen changed them.
	Apply,
	// Never apply them. For a player who wants their own settings left alone.
	Skip,
};

struct PluginConfig {
	int TcpPort;
	// Where the logs are written. Empty means the process's current directory,
	// which is what every earlier version did.
	std::wstring LogDirectory;
	// Stamp every line of the tag log with the wall clock. Off by default: the
	// log is read far more often than it is joined against anything, and the
	// stamps double the width of a line that is otherwise a field and a value.
	// Turn it on to line the log up against another process's record of the
	// same run -- what the game reported and what went over the wire are two
	// separate stories, and only a shared clock tells which came first.
	bool LogTimestamps;
	// One line per action saying where its time went. Off by default: a route
	// sends thousands of actions, and this is only wanted while the latency is
	// being measured.
	bool LogActionTiming;
	// Whether the plugin writes its logs at all. The release build will decide
	// this at compile time; here it is a switch, because a run that is being
	// measured wants the logs and a run that is being played does not.
	bool Logs;
	// Where Neuro is. Empty means "not configured here", and the environment
	// variable decides instead -- a file that names an address is more
	// deliberate than a variable left over in a shell, so the file wins.
	std::string WsUrl;
	FirstRunMode FirstRun;
	// Actions never registered with Neuro, by name. Ordinary text as written in
	// the file, separated by commas.
	std::string DisabledActions;
	// The red dot that blinks in the corner while the link is down.
	bool LinkIndicator;
};

PluginConfig const &GetPluginConfig();

// Applies the settings in `text` on top of the defaults. Separate from reading
// the file so the parsing can be tested without one.
PluginConfig ParsePluginConfig(std::wstring const &text);

// Applies `-neuro-<key>=<value>` arguments on top of what the file said. The
// command line wins: it is typed for one launch, while the file is what the
// installation does every time.
//
// The engine takes unknown `-name=value` arguments without complaint, so these
// travel in an ordinary shortcut. Keys are the file's, and '-' is accepted
// where the file writes '_', so -neuro-ws-url and -neuro-ws_url are one thing.
void ApplyCommandLine(PluginConfig &config, std::wstring const &commandLine);

// The config file named by -neuro-config, empty when none is. Read before the
// file itself, because it decides which file that is.
std::wstring CommandLineConfigPath(std::wstring const &commandLine);

// The file the settings came from, for the plugin to report once logging is up.
// Empty when no file was found and none could be written.
std::wstring const &GetPluginConfigSource();

// Whether that file is one this run wrote, rather than one it found. Reported
// with the source, because "created" and "read" are different answers to "where
// did these settings come from".
bool PluginConfigWasCreated();

// Whether `name` is in DisabledActions. Compared whole, so "skip" does not
// match "skip_movie".
bool PluginConfigActionDisabled(char const *name);

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
// process, which exe and which config produced it, and which directory that
// config sent the logs to.
std::string GetInstanceBanner();
