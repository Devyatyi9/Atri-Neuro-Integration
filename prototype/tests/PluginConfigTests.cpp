#include "PluginConfig.h"

#include "TestMain.h"
#include "tooling/NeuroTCPServer.h"

// The settings that stop two copies of the game from ruining each other's run.
// Getting one of them silently wrong is worse than not having the file at all:
// a port that quietly stayed at the default means both instances answer on it,
// and on Windows that binds without complaint.

namespace {

void ExpectPort(wchar_t const *text, int expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.TcpPort == expected, __FILE__, __LINE__, "tcp_port");
}

void ExpectLogDir(wchar_t const *text, wchar_t const *expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.LogDirectory == expected, __FILE__, __LINE__, "log_dir");
}

void ExpectTimestamps(wchar_t const *text, bool expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.LogTimestamps == expected, __FILE__, __LINE__,
	            "log_timestamps");
}

void ExpectWsUrl(wchar_t const *text, char const *expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.WsUrl == expected, __FILE__, __LINE__, "ws_url");
}

void ExpectLogs(wchar_t const *text, bool expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.Logs == expected, __FILE__, __LINE__, "logs");
}

void ExpectIndicator(wchar_t const *text, bool expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.LinkIndicator == expected, __FILE__, __LINE__,
	            "link_indicator");
}

void ExpectFirstRun(wchar_t const *text, FirstRunMode expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.FirstRun == expected, __FILE__, __LINE__, "first_run");
}

void ExpectDisabled(wchar_t const *text, char const *expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.DisabledActions == expected, __FILE__, __LINE__,
	            "disabled_actions");
}

PluginConfig FromCommandLine(wchar_t const *file, wchar_t const *commandLine) {
	PluginConfig config = ParsePluginConfig(file);
	ApplyCommandLine(config, commandLine);
	return config;
}

}  // namespace

void RunPluginConfigTests() {
	// No file, or nothing in it, is the shipped behaviour.
	ExpectPort(L"", NEURO_TCP_PORT);
	ExpectLogDir(L"", L"");

	ExpectPort(L"tcp_port = 18701", 18701);
	ExpectPort(L"tcp_port=18701", 18701);
	ExpectPort(L"   tcp_port   =   18701   ", 18701);

	// Line endings: the file is hand-edited on Windows, so CRLF is the norm.
	ExpectPort(L"tcp_port = 18701\r\n", 18701);
	ExpectPort(L"# a comment\r\ntcp_port = 18702\r\nlog_dir = D:\\logs\r\n",
	           18702);
	ExpectLogDir(L"# a comment\r\ntcp_port = 18702\r\nlog_dir = D:\\logs\r\n",
	             L"D:\\logs");

	// Comments and blank lines carry no settings.
	ExpectPort(L"# tcp_port = 9999", NEURO_TCP_PORT);
	ExpectPort(L"; tcp_port = 9999", NEURO_TCP_PORT);
	ExpectPort(L"\n\n   \n", NEURO_TCP_PORT);

	// A key with no value clears the directory rather than being ignored: that
	// is how the example file spells "the game's own folder".
	ExpectLogDir(L"log_dir =", L"");

	// Refusing a bad port matters more than accepting a good one. Keeping the
	// default here is deliberate -- a port that cannot bind would leave the
	// plugin unreachable with nothing said about why.
	ExpectPort(L"tcp_port = 0", NEURO_TCP_PORT);
	ExpectPort(L"tcp_port = -1", NEURO_TCP_PORT);
	ExpectPort(L"tcp_port = 65536", NEURO_TCP_PORT);
	ExpectPort(L"tcp_port = nonsense", NEURO_TCP_PORT);
	ExpectPort(L"tcp_port", NEURO_TCP_PORT);

	// A config written for a later version must not stop this one starting.
	ExpectPort(L"future_setting = 1\r\ntcp_port = 18703", 18703);

	// Last one wins, so an override can be appended to a shared file.
	ExpectPort(L"tcp_port = 18701\r\ntcp_port = 18704", 18704);

	// Timestamps are off unless asked for: the log's shape is what every tool
	// reading it so far expects.
	ExpectTimestamps(L"", false);
	ExpectTimestamps(L"log_timestamps = on", true);
	ExpectTimestamps(L"log_timestamps = true", true);
	ExpectTimestamps(L"log_timestamps = yes", true);
	ExpectTimestamps(L"log_timestamps = 1", true);
	ExpectTimestamps(L"log_timestamps = off", false);
	ExpectTimestamps(L"log_timestamps = 0", false);
	// A value nobody can read as a switch leaves the setting alone rather than
	// being taken for "on": a run silently logged in the other format is worse
	// than one that ignored the line.
	ExpectTimestamps(L"log_timestamps = maybe", false);
	ExpectTimestamps(L"log_timestamps = on\r\nlog_timestamps = maybe", true);

	// The address. Empty by default, which is what lets the environment
	// variable decide; anything written here is used as written.
	ExpectWsUrl(L"", "");
	ExpectWsUrl(L"ws_url = ws://127.0.0.1:8001", "ws://127.0.0.1:8001");
	ExpectWsUrl(L"ws_url =", "");
	// Written without a scheme it still has to work: that is how an address is
	// written down everywhere else, and the SDK cannot connect without one.
	ExpectWsUrl(L"ws_url = 127.0.0.1:8000", "ws://127.0.0.1:8000");
	ExpectWsUrl(L"ws_url = wss://example.test/neuro", "wss://example.test/neuro");

	// The switches that are on unless turned off, which is the opposite of the
	// timestamps: a plugin that quietly stopped logging or stopped showing the
	// link would look like one that stopped working.
	ExpectLogs(L"", true);
	ExpectLogs(L"logs = off", false);
	ExpectLogs(L"logs = nonsense", true);
	ExpectIndicator(L"", true);
	ExpectIndicator(L"link_indicator = off", false);
	ExpectIndicator(L"link_indicator = nonsense", true);

	// The one-time settings: applied once unless the file says otherwise.
	ExpectFirstRun(L"", FirstRunMode::Auto);
	ExpectFirstRun(L"first_run = apply", FirstRunMode::Apply);
	ExpectFirstRun(L"first_run = skip", FirstRunMode::Skip);
	ExpectFirstRun(L"first_run = nonsense", FirstRunMode::Auto);

	// The list is kept as written; reading a name out of it is tested through
	// the file, not here, because the lookup goes through the loaded config.
	ExpectDisabled(L"", "");
	ExpectDisabled(L"disabled_actions = skip_movie", "skip_movie");
	ExpectDisabled(L"disabled_actions = skip_movie, read_backlog",
	               "skip_movie, read_backlog");
	// The command line is typed for one launch and beats the file, which is
	// what the installation does every time.
	{
		PluginConfig const config =
		    FromCommandLine(L"tcp_port = 18700", L"game.exe -neuro-tcp-port=18705");
		ReportCheck(config.TcpPort == 18705, __FILE__, __LINE__, "argument wins");
	}
	{
		// Both spellings of the key: a command line reads with hyphens and
		// the file is written with underscores.
		PluginConfig const config =
		    FromCommandLine(L"", L"game.exe -neuro-ws_url=127.0.0.1:9000");
		ReportCheck(config.WsUrl == "ws://127.0.0.1:9000", __FILE__, __LINE__,
		            "underscore key");
	}
	{
		PluginConfig const config =
		    FromCommandLine(L"logs = on", L"game.exe --neuro-logs=off");
		ReportCheck(!config.Logs, __FILE__, __LINE__, "double dash");
	}
	{
		// Quotes hold a path with a space in it together, and are not part
		// of the value.
		PluginConfig const config =
		    FromCommandLine(L"", L"game.exe \"-neuro-log-dir=D:\\two words\"");
		ReportCheck(config.LogDirectory == L"D:\\two words", __FILE__, __LINE__,
		            "quoted value");
	}
	{
		// Anything that is not ours is left alone: the engine has its own
		// arguments on the same line.
		PluginConfig const config = FromCommandLine(
		    L"tcp_port = 18700", L"game.exe -fullscreen -neurolog=off -other=1");
		ReportCheck(config.TcpPort == 18700, __FILE__, __LINE__, "foreign args");
	}
	ReportCheck(CommandLineConfigPath(L"game.exe -neuro-config=D:\\a.ini") ==
	                L"D:\\a.ini",
	            __FILE__, __LINE__, "config path");
	ReportCheck(CommandLineConfigPath(L"game.exe").empty(), __FILE__, __LINE__,
	            "no config path");
}
