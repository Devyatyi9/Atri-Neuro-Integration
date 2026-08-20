#include "TJSExecutor.h"

#include <atomic>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "Logging.h"
#include "PluginLifecycle.h"
#include "UtfConversion.h"

static std::atomic<bool> g_TJSReady(false);
static iTJSDispatch2 *g_ScriptDispatch = nullptr;
static DWORD g_MainThreadId = 0;
static HWND g_GameWindow = nullptr;
static WNDPROC g_OriginalWndProc = nullptr;
static const UINT WM_TJS_EXECUTE = WM_USER + 100;
static const UINT WM_TJS_EXECUTE_SYNC = WM_USER + 101;
static std::atomic<bool> g_AcceptTJSCalls(false);
static std::atomic<bool> g_BlockNextTJSSyncCall(false);
static std::atomic<bool> g_HoldTJSSyncCallsForStressTest(false);

struct TJSSyncCallData {
	std::wstring code;
	tTJSVariant result;
	HANDLE completedEvent;
	std::atomic<bool> cancelled;
	bool executed;
	// Resolved on the caller's thread so the game thread does not re-derive it.
	bool useScriptApi;

	TJSSyncCallData()
	    : completedEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	      cancelled(false),
	      executed(false),
	      useScriptApi(false) { }

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
struct TJSAsyncCall {
	std::wstring code;
	bool useScriptApi;
};

static std::mutex g_TJSAsyncCallsMutex;
static std::unordered_map<UINT_PTR, TJSAsyncCall> g_TJSAsyncCalls;
static std::atomic<UINT_PTR> g_NextTJSAsyncCallId(1);

static TJSAsyncCall TakeTJSAsyncCall(UINT_PTR id) {
	std::lock_guard<std::mutex> lock(g_TJSAsyncCallsMutex);
	auto it = g_TJSAsyncCalls.find(id);
	if (it == g_TJSAsyncCalls.end()) {
		return {};
	}
	auto call = std::move(it->second);
	g_TJSAsyncCalls.erase(it);
	return call;
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

// The single place that hands code to the engine. TJS exposes two entry points
// with different grammars, and picking the wrong one is a syntax error rather
// than a soft failure, so the choice is resolved once by the caller and simply
// applied here. Must run on the game main thread.
static void RunThroughEngine(std::wstring const &code,
                             tTJSVariant *result,
                             bool useScriptApi) {
	if (useScriptApi) {
		TVPExecuteScript(ttstr(code.c_str()), result);
	} else {
		TVPExecuteExpression(ttstr(code.c_str()), result);
	}
}

// Defined below, next to the top-level separator scan it builds on.
static bool ResolveUseScriptApi(std::wstring const &code, TJSExecMode mode);

static std::atomic<int> g_SubclassCallCount(0);

static LRESULT CALLBACK SubclassedWndProc(HWND hwnd,
                                          UINT msg,
                                          WPARAM wParam,
                                          LPARAM lParam) {
	g_SubclassCallCount++;

	switch (msg) {
		case WM_CLOSE: {
			// The host does not call V2Unlink during normal process exit.
			WNDPROC const originalWndProc = g_OriginalWndProc;
			ShutdownPluginWorkers();
			return CallWindowProc(originalWndProc, hwnd, msg, wParam, lParam);
		}

		case WM_TJS_EXECUTE: {
			Log("  [Subclass] WM_TJS_EXECUTE received!\n");
			auto call = TakeTJSAsyncCall(static_cast<UINT_PTR>(wParam));
			if (!call.code.empty() && g_TJSReady.load()) {
				Log("  [Subclass] Executing TJS async\n");
				tTJSVariant dummy;
				try {
					RunThroughEngine(call.code, &dummy, call.useScriptApi);
				} catch (...) {
					Log("  [ExecuteTJSAsync] TJS execution threw an exception: ");
					std::string utf8Code;
					if (Utf16ToUtf8(call.code.c_str(), call.code.size(), utf8Code)) {
						Log(utf8Code.c_str());
					}
					Log("\n");
				}
			}
			return 0;
		}

		case WM_TJS_EXECUTE_SYNC: {
			if (g_HoldTJSSyncCallsForStressTest.load(std::memory_order_acquire)) {
				Log("[Stress] Holding TJS sync dispatch pending unload\n");
				return 0;
			}
			if (g_BlockNextTJSSyncCall.exchange(false, std::memory_order_acq_rel)) {
				Log("[Stress] Blocking next TJS sync dispatch\n");
				while (g_AcceptTJSCalls.load(std::memory_order_acquire)) {
					Sleep(10);
				}
				Log("[Stress] TJS sync dispatch unblocked by shutdown\n");
			}
			auto data = TakeTJSSyncCall(static_cast<UINT_PTR>(wParam));
			if (data) {
				try {
					if (!data->cancelled.load(std::memory_order_acquire) &&
					    !data->code.empty() && g_TJSReady.load()) {
						RunThroughEngine(data->code, &data->result, data->useScriptApi);
						data->executed = true;
					}
				} catch (...) {
					Log("  [ExecuteTJSSync] TJS execution threw an exception\n");
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

			// NOLINTNEXTLINE(performance-no-int-to-ptr): EnumWindows API payload.
			auto *data = reinterpret_cast<EnumWindowsData *>(lParam);
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
	Log("Waiting for game window...\n");

	// The script engine can be ready before the visible game window exists.
	for (int attempt = 0; attempt < 300; attempt++) {
		EnumWindowsData enumData = {nullptr, nullptr};
		EnumWindows(EnumWindowsCallback, (LPARAM)&enumData);

		g_GameWindow = enumData.kirikiriWindow ? enumData.kirikiriWindow
		                                       : enumData.foundWindow;

		if (g_GameWindow) {
			char buf[128];
			sprintf_s(buf, "  Found window on attempt %d\n", attempt + 1);
			Log(buf);
			break;
		}

		Sleep(100);
	}

	if (!g_GameWindow) {
		Log("ERROR: Could not find game window after 30 seconds\n");
		return false;
	}

	char buf[512];
	sprintf_s(buf, "Found game window: HWND=%p\n", g_GameWindow);
	Log(buf);

	wchar_t className[256] = {0};
	wchar_t windowTitle[256] = {0};
	GetClassNameW(g_GameWindow, className, 256);
	GetWindowTextW(g_GameWindow, windowTitle, 256);

	char diagBuf[1024];
	sprintf_s(diagBuf, "  Window class: %ls\n  Window title: %ls\n", className,
	          windowTitle);
	Log(diagBuf);

	g_MainThreadId = GetWindowThreadProcessId(g_GameWindow, NULL);
	DWORD currentThreadId = GetCurrentThreadId();
	sprintf_s(diagBuf, "  Window thread (main): %lu, Current thread: %lu\n",
	          g_MainThreadId, currentThreadId);
	Log(diagBuf);

	LONG_PTR originalWndProc =
	    SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC,
	                     reinterpret_cast<LONG_PTR>(SubclassedWndProc));
	g_OriginalWndProc = reinterpret_cast<WNDPROC>(
	    originalWndProc);  // NOLINT(performance-no-int-to-ptr): SetWindowLongPtr
	                       // stores WNDPROC as LONG_PTR.

	if (!g_OriginalWndProc) {
		DWORD err = GetLastError();
		sprintf_s(diagBuf, "ERROR: SetWindowLongPtr failed, error=%lu\n", err);
		Log(diagBuf);
		g_GameWindow = nullptr;
		return false;
	}

	Log("Game window subclassed successfully\n");
	g_AcceptTJSCalls.store(true, std::memory_order_release);
	return true;
}

static void UnsubclassGameWindow() {
	StopAcceptingTJSCalls();
	if (g_GameWindow && g_OriginalWndProc) {
		SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
		Log("Game window unsubclassed\n");
	}
	g_GameWindow = nullptr;
	g_OriginalWndProc = nullptr;
}

void StopAcceptingTJSCalls() {
	g_AcceptTJSCalls.store(false, std::memory_order_release);
	g_HoldTJSSyncCallsForStressTest.store(false, std::memory_order_release);
	CancelTJSAsyncCalls();
	CancelTJSSyncCalls();
}

void BlockNextTJSSyncCallForStressTest() {
	g_BlockNextTJSSyncCall.store(true, std::memory_order_release);
}

void HoldTJSSyncCallsForStressTest() {
	g_HoldTJSSyncCallsForStressTest.store(true, std::memory_order_release);
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

void ExecuteTJSAsync(wchar_t const *code, TJSExecMode mode) {
	if (!g_AcceptTJSCalls.load(std::memory_order_acquire) || !g_GameWindow ||
	    !IsWindow(g_GameWindow) || !code || !*code) {
		return;
	}

	TJSAsyncCall call;
	call.useScriptApi = ResolveUseScriptApi(code, mode);
	call.code = code;

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
		g_TJSAsyncCalls.emplace(callId, std::move(call));
	}

	if (!PostMessage(g_GameWindow, WM_TJS_EXECUTE, static_cast<WPARAM>(callId),
	                 0)) {
		TakeTJSAsyncCall(callId);
		Log("  [ExecuteTJSAsync] Failed to post to the game window\n");
	}
}

// Returns true when the code contains a top-level statement separator, i.e. a
// ';' or newline that is not inside a string literal, comment, or balanced
// bracket pair. Statements must then run in a function body; the expression
// wrapper `return (CODE)` is a syntax error for them.
static bool HasTopLevelSeparator(std::wstring const &code) {
	int depth = 0;
	bool inSingle = false;
	bool inDouble = false;
	bool inLineComment = false;
	bool inBlockComment = false;
	for (size_t i = 0; i < code.size(); ++i) {
		wchar_t const c = code[i];
		if (inLineComment) {
			if (c == L'\n') {
				inLineComment = false;
			}
			continue;
		}
		if (inBlockComment) {
			if (c == L'*' && i + 1 < code.size() && code[i + 1] == L'/') {
				inBlockComment = false;
				++i;
			}
			continue;
		}
		if (inSingle) {
			if (c == L'\\' && i + 1 < code.size()) {
				++i;
			} else if (c == L'\'') {
				inSingle = false;
			}
			continue;
		}
		if (inDouble) {
			if (c == L'\\' && i + 1 < code.size()) {
				++i;
			} else if (c == L'"') {
				inDouble = false;
			}
			continue;
		}
		if (c == L'\'' && (i == 0 || code[i - 1] != L'\\')) {
			inSingle = true;
			continue;
		}
		if (c == L'"' && (i == 0 || code[i - 1] != L'\\')) {
			inDouble = true;
			continue;
		}
		if (c == L'/' && i + 1 < code.size() && code[i + 1] == L'/') {
			inLineComment = true;
			++i;
			continue;
		}
		if (c == L'/' && i + 1 < code.size() && code[i + 1] == L'*') {
			inBlockComment = true;
			++i;
			continue;
		}
		switch (c) {
			case L'(':
			case L'[':
			case L'{':
				++depth;
				break;
			case L')':
			case L']':
			case L'}':
				--depth;
				break;
			case L';':
			case L'\n':
				if (depth == 0) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

// Returns true when the first real token is a keyword that can only begin a
// statement. This closes the blind spot in the separator scan: a snippet that
// is one `if (...) { ... }` block keeps every ';' and newline inside the
// braces, so the scan sees no top-level separator and would pick the expression
// branch, where a statement is a syntax error.
static bool StartsWithStatementKeyword(std::wstring const &code) {
	size_t i = 0;
	while (i < code.size()) {
		wchar_t const c = code[i];
		if (iswspace(c)) {
			++i;
			continue;
		}
		if (c == L'/' && i + 1 < code.size() && code[i + 1] == L'/') {
			i += 2;
			while (i < code.size() && code[i] != L'\n') {
				++i;
			}
			continue;
		}
		if (c == L'/' && i + 1 < code.size() && code[i + 1] == L'*') {
			i += 2;
			while (i + 1 < code.size() && !(code[i] == L'*' && code[i + 1] == L'/')) {
				++i;
			}
			i += 2;
			continue;
		}
		break;
	}

	static wchar_t const *const kStatementKeywords[] = {
	    L"if",    L"while",  L"for",      L"do",    L"switch", L"try",
	    L"var",   L"return", L"function", L"with",  L"break",  L"continue",
	    L"throw", L"class",  L"property", L"delete"};
	for (auto const *keyword : kStatementKeywords) {
		size_t const length = wcslen(keyword);
		if (code.compare(i, length, keyword) != 0) {
			continue;
		}
		// Reject a longer identifier that merely starts with the keyword.
		size_t const next = i + length;
		if (next >= code.size() || (!iswalnum(code[next]) && code[next] != L'_')) {
			return true;
		}
	}
	return false;
}

// Resolves Auto and reports whether the snippet must go through the script
// entry point. Explicit modes are honoured as given.
static bool ResolveUseScriptApi(std::wstring const &code, TJSExecMode mode) {
	if (mode == TJSExecMode::Script)
		return true;
	if (mode == TJSExecMode::Expression)
		return false;
	return HasTopLevelSeparator(code) || StartsWithStatementKeyword(code);
}

// Wraps a snippet so a thrown error comes back as a readable string instead of
// only a false return. Real TJS exceptions are objects carrying `.message`; a
// `throw` of a primitive lands in `catch` as a string, so `typeof e ==
// 'Object'` selects the readable form for each case.
static std::wstring WrapTJSProbe(std::wstring const &code, bool useScriptApi) {
	if (useScriptApi) {
		// TVPExecuteScript parses statements natively, so no IIFE is needed: a
		// top-level `try`/`catch` is legal here and a top-level `return` inside
		// the caller's code still sets the result. The trailing ';' guards
		// against a missing final separator (TJS has no automatic semicolon
		// insertion).
		return L"try { " + code +
		       L"\n; } catch(e) { return '!!!TJS_EXCEPTION!!! ' + "
		       L"(typeof e == 'Object' ? e.message : e); }";
	}
	// TVPExecuteExpression needs a single expression; the IIFE is what makes a
	// try/catch expressible here.
	return L"(function(){ try { return (" + code +
	       L"); } catch(e) { return '!!!TJS_EXCEPTION!!! ' + "
	       L"(typeof e == 'Object' ? e.message : e); } })()";
}

bool ExecuteTJSSync(wchar_t const *code,
                    tTJSVariant *result,
                    DWORD timeoutMs,
                    TJSExecMode mode) {
	if (!g_AcceptTJSCalls.load(std::memory_order_acquire) || !g_GameWindow ||
	    !IsWindow(g_GameWindow) || !code)
		return false;

	bool const useScriptApi = ResolveUseScriptApi(code, mode);
	std::wstring wrappedCode = WrapTJSProbe(code, useScriptApi);

	if (IsMainThread()) {
		Log("[ExecuteTJSSync] Running on main thread\n");
		if (g_TJSReady.load()) {
			try {
				RunThroughEngine(wrappedCode, result, useScriptApi);
				return true;
			} catch (...) {
				Log("[ExecuteTJSSync] TJS execution threw an exception\n");
				return false;
			}
		}
		Log("[ExecuteTJSSync] TJS not ready on main thread\n");
		return false;
	}

	// Pending state is map-owned. Messages carry IDs, so timeout and teardown
	// cannot leave a dangling pointer in the window queue.
	auto data = std::make_shared<TJSSyncCallData>();
	data->code = wrappedCode;
	data->useScriptApi = useScriptApi;

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
		Log("  [ExecuteTJSSync] Timed out waiting for the game thread\n");
	} else if (!executed) {
		// The game thread answered but skipped the call (cancelled, or TJS not
		// ready). This used to return false with no trace at all, which hid the
		// cause of dropped polls from every caller.
		Log("  [ExecuteTJSSync] Game thread skipped the call (not executed)\n");
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
			Log("TJS initialized successfully!\n");
			OutputDebugStringW(L"[NEURO PLUGIN] TJS initialized!\n");
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Log("TryInitTJS: EXCEPTION caught!\n");
	}
}

bool IsTJSReady() {
	if (g_TJSReady.load())
		return true;
	TryInitTJS();
	return g_TJSReady.load();
}
