#include "TJSExecutor.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

#include "Logging.h"

static std::atomic<bool> g_TJSReady(false);
static iTJSDispatch2 *g_ScriptDispatch = nullptr;
static DWORD g_MainThreadId = 0;
static HWND g_GameWindow = nullptr;
static WNDPROC g_OriginalWndProc = nullptr;
static const UINT WM_TJS_EXECUTE = WM_USER + 100;
static const UINT WM_TJS_EXECUTE_SYNC = WM_USER + 101;
static std::atomic<bool> g_AcceptTJSCalls(false);

struct TJSSyncCallData {
	std::wstring code;
	tTJSVariant result;
	HANDLE completedEvent;
	std::atomic<bool> cancelled;
	bool executed;

	TJSSyncCallData()
	    : completedEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	      cancelled(false),
	      executed(false) { }

	~TJSSyncCallData() {
		if (completedEvent) {
			CloseHandle(completedEvent);
		}
	}
};

static std::mutex g_TJSSyncCallsMutex;
static std::unordered_map<UINT_PTR, std::shared_ptr<TJSSyncCallData>>
    g_TJSSyncCalls;
static std::atomic<UINT_PTR> g_NextTJSSyncCallId(1);
static std::mutex g_TJSAsyncCallsMutex;
static std::unordered_map<UINT_PTR, std::wstring> g_TJSAsyncCalls;
static std::atomic<UINT_PTR> g_NextTJSAsyncCallId(1);

static std::wstring TakeTJSAsyncCall(UINT_PTR id) {
	std::lock_guard<std::mutex> lock(g_TJSAsyncCallsMutex);
	auto it = g_TJSAsyncCalls.find(id);
	if (it == g_TJSAsyncCalls.end()) {
		return {};
	}
	auto code = std::move(it->second);
	g_TJSAsyncCalls.erase(it);
	return code;
}

static void CancelTJSAsyncCalls() {
	std::lock_guard<std::mutex> lock(g_TJSAsyncCallsMutex);
	g_TJSAsyncCalls.clear();
}

static std::shared_ptr<TJSSyncCallData> TakeTJSSyncCall(UINT_PTR id) {
	std::lock_guard<std::mutex> lock(g_TJSSyncCallsMutex);
	auto it = g_TJSSyncCalls.find(id);
	if (it == g_TJSSyncCalls.end()) {
		return nullptr;
	}
	auto call = std::move(it->second);
	g_TJSSyncCalls.erase(it);
	return call;
}

static void CancelTJSSyncCalls() {
	std::unordered_map<UINT_PTR, std::shared_ptr<TJSSyncCallData>> calls;
	{
		std::lock_guard<std::mutex> lock(g_TJSSyncCallsMutex);
		calls.swap(g_TJSSyncCalls);
	}
	for (auto const &entry : calls) {
		entry.second->cancelled.store(true, std::memory_order_release);
		SetEvent(entry.second->completedEvent);
	}
}

bool IsMainThread() {
	return GetCurrentThreadId() == g_MainThreadId;
}

static std::atomic<int> g_SubclassCallCount(0);

static LRESULT CALLBACK SubclassedWndProc(HWND hwnd,
                                          UINT msg,
                                          WPARAM wParam,
                                          LPARAM lParam) {
	g_SubclassCallCount++;

	switch (msg) {
		case WM_TJS_EXECUTE: {
			LogToFile("  [Subclass] WM_TJS_EXECUTE received!\n");
			auto code = TakeTJSAsyncCall(static_cast<UINT_PTR>(wParam));
			if (!code.empty() && g_TJSReady.load()) {
				LogToFile("  [Subclass] Executing TJS async\n");
				tTJSVariant dummy;
				try {
					TVPExecuteExpression(ttstr(code.c_str()), &dummy);
				} catch (...) {
					LogToFile("  [ExecuteTJSAsync] TJS execution threw an exception\n");
				}
			}
			return 0;
		}

		case WM_TJS_EXECUTE_SYNC: {
			auto data = TakeTJSSyncCall(static_cast<UINT_PTR>(wParam));
			if (data) {
				try {
					if (!data->cancelled.load(std::memory_order_acquire) &&
					    !data->code.empty() && g_TJSReady.load()) {
						TVPExecuteExpression(ttstr(data->code.c_str()), &data->result);
						data->executed = true;
					}
				} catch (...) {
					LogToFile("  [ExecuteTJSSync] TJS execution threw an exception\n");
				}
				SetEvent(data->completedEvent);
			}
			return 0;
		}
		default:
			return CallWindowProc(g_OriginalWndProc, hwnd, msg, wParam, lParam);
	}
}

struct EnumWindowsData {
	HWND foundWindow;
	HWND kirikiriWindow;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
	DWORD windowPid;
	GetWindowThreadProcessId(hwnd, &windowPid);

	if (windowPid == GetCurrentProcessId()) {
		if (IsWindowVisible(hwnd) && GetParent(hwnd) == NULL) {
			wchar_t className[256] = {0};
			GetClassNameW(hwnd, className, 256);

			EnumWindowsData *data = (EnumWindowsData *)lParam;
			if (wcscmp(className, L"#32770") == 0) {
				return TRUE;
			}

			if (wcsstr(className, L"TTVPWindowForm") != nullptr ||
			    wcsstr(className, L"TVPMainWindow") != nullptr) {
				data->kirikiriWindow = hwnd;
				return FALSE;
			}

			if (!data->foundWindow) {
				data->foundWindow = hwnd;
			}
		}
	}
	return TRUE;
}

static bool SubclassGameWindow() {
	LogToFile("Waiting for game window...\n");

	for (int attempt = 0; attempt < 50; attempt++) {
		EnumWindowsData enumData = {nullptr, nullptr};
		EnumWindows(EnumWindowsCallback, (LPARAM)&enumData);

		g_GameWindow = enumData.kirikiriWindow ? enumData.kirikiriWindow
		                                       : enumData.foundWindow;

		if (g_GameWindow) {
			char buf[128];
			sprintf_s(buf, "  Found window on attempt %d\n", attempt + 1);
			LogToFile(buf);
			break;
		}

		Sleep(100);
	}

	if (!g_GameWindow) {
		LogToFile("ERROR: Could not find game window after 5 seconds\n");
		return false;
	}

	char buf[512];
	sprintf_s(buf, "Found game window: HWND=%p\n", g_GameWindow);
	LogToFile(buf);

	wchar_t className[256] = {0};
	wchar_t windowTitle[256] = {0};
	GetClassNameW(g_GameWindow, className, 256);
	GetWindowTextW(g_GameWindow, windowTitle, 256);

	char diagBuf[1024];
	sprintf_s(diagBuf, "  Window class: %ls\n  Window title: %ls\n", className,
	          windowTitle);
	LogToFile(diagBuf);

	g_MainThreadId = GetWindowThreadProcessId(g_GameWindow, NULL);
	DWORD currentThreadId = GetCurrentThreadId();
	sprintf_s(diagBuf, "  Window thread (main): %lu, Current thread: %lu\n",
	          g_MainThreadId, currentThreadId);
	LogToFile(diagBuf);

	g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC,
	                                              (LONG_PTR)SubclassedWndProc);

	if (!g_OriginalWndProc) {
		DWORD err = GetLastError();
		sprintf_s(diagBuf, "ERROR: SetWindowLongPtr failed, error=%lu\n", err);
		LogToFile(diagBuf);
		g_GameWindow = nullptr;
		return false;
	}

	LogToFile("Game window subclassed successfully\n");
	g_AcceptTJSCalls.store(true, std::memory_order_release);
	return true;
}

static void UnsubclassGameWindow() {
	g_AcceptTJSCalls.store(false, std::memory_order_release);
	CancelTJSAsyncCalls();
	CancelTJSSyncCalls();
	if (g_GameWindow && g_OriginalWndProc) {
		SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
		LogToFile("Game window unsubclassed\n");
	}
	g_GameWindow = nullptr;
	g_OriginalWndProc = nullptr;
}

bool InitializeTJSExecutor() {
	return SubclassGameWindow();
}

void ShutdownTJSExecutor() {
	UnsubclassGameWindow();
	if (g_ScriptDispatch) {
		g_ScriptDispatch->Release();
		g_ScriptDispatch = nullptr;
	}
	g_TJSReady.store(false);
}

void ExecuteTJSAsync(wchar_t const *code) {
	if (!g_AcceptTJSCalls.load(std::memory_order_acquire) || !g_GameWindow ||
	    !IsWindow(g_GameWindow) || !code || !*code) {
		return;
	}

	UINT_PTR callId;
	{
		std::lock_guard<std::mutex> lock(g_TJSAsyncCallsMutex);
		if (!g_AcceptTJSCalls.load(std::memory_order_acquire)) {
			return;
		}
		do {
			callId = g_NextTJSAsyncCallId.fetch_add(1, std::memory_order_relaxed);
		} while (callId == 0 ||
		         g_TJSAsyncCalls.find(callId) != g_TJSAsyncCalls.end());
		g_TJSAsyncCalls.emplace(callId, code);
	}

	if (!PostMessage(g_GameWindow, WM_TJS_EXECUTE, static_cast<WPARAM>(callId),
	                 0)) {
		TakeTJSAsyncCall(callId);
		LogToFile("  [ExecuteTJSAsync] Failed to post to the game window\n");
	}
}

bool ExecuteTJSSync(wchar_t const *code, tTJSVariant *result, DWORD timeoutMs) {
	if (!g_AcceptTJSCalls.load(std::memory_order_acquire) || !g_GameWindow ||
	    !IsWindow(g_GameWindow) || !code)
		return false;

	std::wstring wrappedCode =
	    L"(function(){ try { return (" + std::wstring(code) +
	    L"); } catch(e) { return '!!!TJS_EXCEPTION!!! ' + e.message; } })()";

	if (IsMainThread()) {
		LogToFile("[ExecuteTJSSync] Running on main thread\n");
		if (g_TJSReady.load()) {
			try {
				TVPExecuteExpression(ttstr(wrappedCode.c_str()), result);
				return true;
			} catch (...) {
				LogToFile("[ExecuteTJSSync] TJS execution threw an exception\n");
				return false;
			}
		}
		LogToFile("[ExecuteTJSSync] TJS not ready on main thread\n");
		return false;
	}

	// Pending state is map-owned. Messages carry IDs, so timeout and teardown
	// cannot leave a dangling pointer in the window queue.
	auto data = std::make_shared<TJSSyncCallData>();
	data->code = wrappedCode;

	if (!data->completedEvent) {
		return false;
	}

	UINT_PTR callId;
	{
		std::lock_guard<std::mutex> lock(g_TJSSyncCallsMutex);
		if (!g_AcceptTJSCalls.load(std::memory_order_acquire)) {
			return false;
		}
		do {
			callId = g_NextTJSSyncCallId.fetch_add(1, std::memory_order_relaxed);
		} while (callId == 0 ||
		         g_TJSSyncCalls.find(callId) != g_TJSSyncCalls.end());
		g_TJSSyncCalls.emplace(callId, data);
	}
	BOOL postResult = PostMessage(g_GameWindow, WM_TJS_EXECUTE_SYNC,
	                              static_cast<WPARAM>(callId), 0);
	if (!postResult) {
		TakeTJSSyncCall(callId);
		return false;
	}

	DWORD waitResult = WaitForSingleObject(data->completedEvent, timeoutMs);
	bool completed = waitResult == WAIT_OBJECT_0;
	bool executed = completed && data->executed;
	if (executed && result) {
		*result = data->result;
	}
	if (!completed) {
		data->cancelled.store(true, std::memory_order_release);
		TakeTJSSyncCall(callId);
		LogToFile("  [ExecuteTJSSync] Timed out waiting for the game thread\n");
	}
	return executed;
}

void TryInitTJS() {
	if (g_TJSReady.load())
		return;

	__try {
		iTJSDispatch2 *dispatch = TVPGetScriptDispatch();
		if (dispatch) {
			g_ScriptDispatch = dispatch;
			g_TJSReady.store(true);
			LogToFile("TJS initialized successfully!\n");
			OutputDebugStringW(L"[NEURO PLUGIN] TJS initialized!\n");
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		LogToFile("TryInitTJS: EXCEPTION caught!\n");
	}
}

bool IsTJSReady() {
	if (g_TJSReady.load())
		return true;
	TryInitTJS();
	return g_TJSReady.load();
}
