// Winsock2 must be included before windows.h.
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <list>
#include <memory>
#include <mutex>
#include <vector>
#include "Logging.h"
#include "NeuroTCPServer.h"
#include "TJSExecutor.h"
#include "ThreadGuard.h"
#include "UtfConversion.h"

// For tTJSVariant and ttstr.
#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#pragma comment(lib, "ws2_32.lib")

// ============================================================================
// Server state.
// ============================================================================
static std::atomic<bool> g_TCPServerRunning(false);
static std::atomic<bool> g_ExitAfterTCPResponse(false);
static std::thread g_TCPServerThread;
static SOCKET g_ListenSocket = INVALID_SOCKET;

// Active client sockets, closed during shutdown.
static std::mutex g_ClientsMutex;
static std::list<SOCKET> g_ActiveClients;
static std::mutex g_ClientThreadsMutex;

struct ClientThread {
	std::thread thread;
	std::shared_ptr<std::atomic<bool>> finished;
	std::shared_ptr<std::atomic<bool>> exitAfterResponse;
};

static std::vector<ClientThread> g_ClientThreads;

// ============================================================================
// Utilities.
// ============================================================================

static void LogTCP(char const *msg) {
	char buf[512];
	sprintf_s(buf, "[TCP] %s\n", msg);
	Log(buf);
}

static void ShutdownActiveClients() {
	std::lock_guard<std::mutex> lock(g_ClientsMutex);
	for (SOCKET socket : g_ActiveClients) {
		shutdown(socket, SD_BOTH);
	}
}

static void JoinClientThreads() {
	std::vector<ClientThread> threads;
	{
		std::lock_guard<std::mutex> lock(g_ClientThreadsMutex);
		threads.swap(g_ClientThreads);
	}

	for (ClientThread &clientThread : threads) {
		clientThread.thread.join();
	}
}

static void ReapFinishedClientThreads() {
	std::vector<ClientThread> finishedThreads;
	bool scheduleExit = false;
	{
		std::lock_guard<std::mutex> lock(g_ClientThreadsMutex);
		for (auto it = g_ClientThreads.begin(); it != g_ClientThreads.end();) {
			if (it->finished->load()) {
				finishedThreads.push_back(std::move(*it));
				it = g_ClientThreads.erase(it);
			} else {
				++it;
			}
		}
	}

	for (ClientThread &clientThread : finishedThreads) {
		clientThread.thread.join();
	}
}

// Send a string to the client: [4-byte LE length][data].
static bool SendToClient(SOCKET sock, std::string const &data) {
	uint32_t len = (uint32_t)data.size();
	int sent = send(sock, (char const *)&len, 4, 0);
	if (sent != 4)
		return false;
	if (len > 0) {
		sent = send(sock, data.data(), (int)len, 0);
		if (sent < 0)
			return false;
	}
	return true;
}

// Receive a string from the client: [4-byte LE length][data].
static bool RecvFromClient(SOCKET sock, std::string &out) {
	out.clear();
	uint32_t len = 0;
	int recved = recv(sock, (char *)&len, 4, 0);
	if (recved != 4)
		return false;
	if (len > NEURO_TCP_MAX_MSG_SIZE) {
		LogTCP("Message too large, rejecting");
		return false;
	}
	if (len > 0) {
		out.resize(len);
		recved = recv(sock, &out[0], (int)len, 0);
		if (recved < 0 || (uint32_t)recved != len)
			return false;
	}
	return true;
}

// ============================================================================
// Single-client handling.
// ============================================================================

static void HandleClient(SOCKET clientSock,
                         std::shared_ptr<std::atomic<bool>> finished,
                         std::shared_ptr<std::atomic<bool>> exitAfterResponse) {
	char buf[128];
	sprintf_s(buf, "[TCP] Client connected (sock=%d)\n", (int)clientSock);
	Log(buf);

	while (g_TCPServerRunning.load()) {
		std::string code;
		if (!RecvFromClient(clientSock, code)) {
			break;
		}

		if (code.empty())
			continue;

		std::wstring wideCode;
		if (!Utf8ToUtf16(code.data(), code.size(), wideCode) ||
		    wideCode.find(L'\0') != std::wstring::npos) {
			SendToClient(clientSock, "error: invalid UTF-8");
			continue;
		}

		// Execute TJS code synchronously, with a 10-second timeout.
		tTJSVariant tjsResult;
		bool ok = ExecuteTJSSync(wideCode.c_str(), &tjsResult, 10000);

		std::string response;
		if (ok) {
			ttstr resultStr(tjsResult);
			if (!Utf16ToUtf8(resultStr.c_str(), resultStr.GetLen(), response)) {
				response = "error: invalid UTF-16 result";
			} else if (response.empty()) {
				response = "(empty)";
			}
		} else {
			response = "error: execution timeout or failed";
		}

		bool const scheduleExitAfterResponse =
		    g_ExitAfterTCPResponse.exchange(false, std::memory_order_acq_rel);
		if (!SendToClient(clientSock, response)) {
			break;
		}
		if (scheduleExitAfterResponse) {
			exitAfterResponse->store(true, std::memory_order_release);
			break;
		}
	}

	// Cleanup.
	closesocket(clientSock);
	{
		std::lock_guard<std::mutex> lock(g_ClientsMutex);
		g_ActiveClients.remove(clientSock);
	}

	sprintf_s(buf, "[TCP] Client disconnected (sock=%d)\n", (int)clientSock);
	Log(buf);
	finished->store(true);
}

// ============================================================================
// Server thread.
// ============================================================================

static void TCPServerThread() {
	LogTCP("Starting TCP server...");

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		LogTCP("WSAStartup failed");
		g_TCPServerRunning.store(false);
		return;
	}

	g_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_ListenSocket == INVALID_SOCKET) {
		LogTCP("Failed to create socket");
		WSACleanup();
		g_TCPServerRunning.store(false);
		return;
	}

	int opt = 1;
	setsockopt(g_ListenSocket, SOL_SOCKET, SO_REUSEADDR, (char const *)&opt,
	           sizeof(opt));

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(NEURO_TCP_PORT);

	if (bind(g_ListenSocket, (sockaddr *)&addr, sizeof(addr)) != 0) {
		char buf[128];
		sprintf_s(buf, "[TCP] Bind failed on port %d\n", NEURO_TCP_PORT);
		Log(buf);
		closesocket(g_ListenSocket);
		g_ListenSocket = INVALID_SOCKET;
		WSACleanup();
		g_TCPServerRunning.store(false);
		return;
	}

	if (listen(g_ListenSocket, NEURO_TCP_MAX_CLIENTS) != 0) {
		LogTCP("Listen failed");
		closesocket(g_ListenSocket);
		g_ListenSocket = INVALID_SOCKET;
		WSACleanup();
		g_TCPServerRunning.store(false);
		return;
	}

	char buf[128];
	sprintf_s(buf, "[TCP] Listening on 127.0.0.1:%d\n", NEURO_TCP_PORT);
	Log(buf);

	while (g_TCPServerRunning.load()) {
		ReapFinishedClientThreads();

		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(g_ListenSocket, &readSet);
		timeval timeout = {0, 200000};
		int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
		if (selectResult == SOCKET_ERROR) {
			if (g_TCPServerRunning.load()) {
				LogTCP("select failed");
			}
			break;
		}
		if (selectResult == 0) {
			continue;
		}

		SOCKET clientSock = accept(g_ListenSocket, NULL, NULL);
		if (clientSock == INVALID_SOCKET) {
			if (g_TCPServerRunning.load()) {
				LogTCP("accept failed");
			}
			break;
		}

		{
			std::lock_guard<std::mutex> lock(g_ClientsMutex);
			g_ActiveClients.push_back(clientSock);
		}

		{
			std::lock_guard<std::mutex> lock(g_ClientThreadsMutex);
			auto finished = std::make_shared<std::atomic<bool>>(false);
			auto exitAfterResponse = std::make_shared<std::atomic<bool>>(false);
			g_ClientThreads.push_back(
			    {std::thread([clientSock, finished, exitAfterResponse] {
				     RunThreadGuarded("tcp client", [&] {
					     HandleClient(clientSock, finished, exitAfterResponse);
				     });
			     }),
			     finished, exitAfterResponse});
		}
	}

	ShutdownActiveClients();
	JoinClientThreads();

	closesocket(g_ListenSocket);
	g_ListenSocket = INVALID_SOCKET;
	WSACleanup();

	LogTCP("TCP server stopped");
}

// ============================================================================
// Public functions.
// ============================================================================

bool StartTCPServer() {
	if (g_TCPServerRunning.load()) {
		LogTCP("Already running");
		return true;
	}
	if (g_TCPServerThread.joinable()) {
		g_TCPServerThread.join();
	}
	g_TCPServerRunning.store(true);
	g_TCPServerThread =
	    std::thread([] { RunThreadGuarded("tcp server", TCPServerThread); });
	return true;
}

void StopTCPServer() {
	g_TCPServerRunning.store(false);

	if (g_ListenSocket != INVALID_SOCKET) {
		shutdown(g_ListenSocket, SD_BOTH);
		closesocket(g_ListenSocket);
		g_ListenSocket = INVALID_SOCKET;
	}
	ShutdownActiveClients();

	if (g_TCPServerThread.joinable()) {
		g_TCPServerThread.join();
	}
}

bool IsTCPServerRunning() {
	return g_TCPServerRunning.load();
}

void ScheduleExitAfterTCPResponse() {
	g_ExitAfterTCPResponse.store(true, std::memory_order_release);
}
