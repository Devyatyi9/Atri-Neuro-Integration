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
	ReportCheck(config.TcpPort == expected, __FILE__, __LINE__,
	            "tcp_port");
}

void ExpectLogDir(wchar_t const *text, wchar_t const *expected) {
	PluginConfig const config = ParsePluginConfig(text);
	ReportCheck(config.LogDirectory == expected, __FILE__, __LINE__, "log_dir");
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
	ExpectPort(L"# a comment\r\ntcp_port = 18702\r\nlog_dir = D:\\logs\r\n", 18702);
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
}
