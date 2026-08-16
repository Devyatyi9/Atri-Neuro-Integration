// Winsock2 должен быть включён ДО windows.h
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <list>
#include <mutex>
#include <vector>
#include "NeuroTCPServer.h"

// Для tTJSVariant и ttstr
#pragma warning(push)
#pragma warning(disable : 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

#pragma comment(lib, "ws2_32.lib")

// Forward declarations from dllmain
void LogToFile(char const *message);
bool ExecuteTJSSync(wchar_t const *code, tTJSVariant *result, DWORD timeoutMs);

// ============================================================================
// Состояние сервера
// ============================================================================
static std::atomic<bool> g_TCPServerRunning(false);
static std::thread g_TCPServerThread;
static SOCKET g_ListenSocket = INVALID_SOCKET;

// Список активных клиентских сокетов (для закрытия при остановке)
static std::mutex g_ClientsMutex;
static std::list<SOCKET> g_ActiveClients;

// ============================================================================
// Утилиты
// ============================================================================

static void LogTCP(char const *msg) {
	char buf[512];
	sprintf_s(buf, "[TCP] %s\n", msg);
	LogToFile(buf);
}

// Отправить строку клиенту: [4-byte LE length][data]
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

// Получить строку от клиента: [4-byte LE length][data]
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
// Обработка одного клиента
// ============================================================================

static void HandleClient(SOCKET clientSock) {
	char buf[128];
	sprintf_s(buf, "[TCP] Client connected (sock=%d)\n", (int)clientSock);
	LogToFile(buf);

	while (g_TCPServerRunning.load()) {
		std::string code;
		if (!RecvFromClient(clientSock, code)) {
			break;
		}

		if (code.empty())
			continue;

		// UTF-8 → UTF-16
		int wideLen =
		    MultiByteToWideChar(CP_UTF8, 0, code.data(), (int)code.size(), NULL, 0);
		if (wideLen <= 0) {
			SendToClient(clientSock, "error: invalid UTF-8");
			continue;
		}
		std::vector<wchar_t> wideBuf(wideLen);
		MultiByteToWideChar(CP_UTF8, 0, code.data(), (int)code.size(),
		                    wideBuf.data(), wideLen);
		std::wstring wideCode(wideBuf.data(), wideLen);

		// Выполняем TJS код (синхронно, до 10 сек)
		tTJSVariant tjsResult;
		bool ok = ExecuteTJSSync(wideCode.c_str(), &tjsResult, 10000);

		std::string response;
		if (ok) {
			ttstr resultStr(tjsResult);
			int utf8Len = WideCharToMultiByte(CP_UTF8, 0, resultStr.c_str(), -1, NULL,
			                                  0, NULL, NULL);
			if (utf8Len > 0) {
				response.resize(utf8Len - 1);
				WideCharToMultiByte(CP_UTF8, 0, resultStr.c_str(), -1, &response[0],
				                    utf8Len, NULL, NULL);
			} else {
				response = "(empty)";
			}
		} else {
			response = "error: execution timeout or failed";
		}

		if (!SendToClient(clientSock, response)) {
			break;
		}
	}

	// Очистка
	closesocket(clientSock);
	{
		std::lock_guard<std::mutex> lock(g_ClientsMutex);
		g_ActiveClients.remove(clientSock);
	}

	sprintf_s(buf, "[TCP] Client disconnected (sock=%d)\n", (int)clientSock);
	LogToFile(buf);
}

// ============================================================================
// Поток сервера
// ============================================================================

static void TCPServerThread() {
	LogTCP("Starting TCP server...");

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		LogTCP("WSAStartup failed");
		return;
	}

	g_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_ListenSocket == INVALID_SOCKET) {
		LogTCP("Failed to create socket");
		WSACleanup();
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
		LogToFile(buf);
		closesocket(g_ListenSocket);
		g_ListenSocket = INVALID_SOCKET;
		WSACleanup();
		return;
	}

	if (listen(g_ListenSocket, NEURO_TCP_MAX_CLIENTS) != 0) {
		LogTCP("Listen failed");
		closesocket(g_ListenSocket);
		g_ListenSocket = INVALID_SOCKET;
		WSACleanup();
		return;
	}

	char buf[128];
	sprintf_s(buf, "[TCP] Listening on 127.0.0.1:%d\n", NEURO_TCP_PORT);
	LogToFile(buf);

	g_TCPServerRunning.store(true);

	while (g_TCPServerRunning.load()) {
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

		std::thread(HandleClient, clientSock).detach();
	}

	{
		std::lock_guard<std::mutex> lock(g_ClientsMutex);
		for (SOCKET s : g_ActiveClients) {
			closesocket(s);
		}
		g_ActiveClients.clear();
	}

	closesocket(g_ListenSocket);
	g_ListenSocket = INVALID_SOCKET;
	WSACleanup();

	LogTCP("TCP server stopped");
}

// ============================================================================
// Публичные функции
// ============================================================================

bool StartTCPServer() {
	if (g_TCPServerRunning.load()) {
		LogTCP("Already running");
		return true;
	}
	if (g_TCPServerThread.joinable()) {
		g_TCPServerThread.join();
	}
	g_TCPServerThread = std::thread(TCPServerThread);
	return true;
}

void StopTCPServer() {
	if (!g_TCPServerRunning.load())
		return;
	g_TCPServerRunning.store(false);

	if (g_ListenSocket != INVALID_SOCKET) {
		closesocket(g_ListenSocket);
		g_ListenSocket = INVALID_SOCKET;
	}

	if (g_TCPServerThread.joinable()) {
		g_TCPServerThread.join();
	}
}

bool IsTCPServerRunning() {
	return g_TCPServerRunning.load();
}
