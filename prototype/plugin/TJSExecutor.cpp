#include "TJSExecutor.h"

#include <atomic>
#include <cstdio>
#include <cwctype>
#include <deque>
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
static std::atomic<bool> g_AcceptTJSCalls(false);
static std::atomic<bool> g_BlockNextTJSSyncCall(false);
static std::atomic<bool> g_HoldTJSSyncCallsForStressTest(false);
static std::atomic<bool> g_TJSContinuousEventHookRegistered(false);
static std::atomic<unsigned long long> g_TJSContinuousCallbackCount(0);
static std::atomic<tjs_uint64> g_TJSLastContinuousTick(0);
static DWORD g_TJSContinuousHookThreadId = 0;

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
			completedEvent = nullptr;
		}
	}

	// Non-copyable, movable only via shared_ptr to prevent handle double-close.
	TJSSyncCallData(TJSSyncCallData const &) = delete;
	TJSSyncCallData &operator=(TJSSyncCallData const &) = delete;
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
static std::deque<UINT_PTR> g_TJSAsyncCallOrder;
static std::atomic<UINT_PTR> g_NextTJSAsyncCallId(1);
constexpr size_t kMaxTJSAsyncCalls = 256;
static std::atomic<unsigned long long> g_TJSAsyncExecutedCount(0);
static std::atomic<unsigned long long> g_TJSAsyncDroppedCount(0);

static void ExecuteTJSAsyncCall(TJSAsyncCall const &call);
static void ExecuteTJSSyncCall(std::shared_ptr<TJSSyncCallData> const &data);
static bool TakeNextTJSAsyncCall(TJSAsyncCall &call);
static std::shared_ptr<TJSSyncCallData> TakeNextTJSSyncCall();

static std::atomic<unsigned long long> g_TJSSyncExecutedCount(0);
static std::deque<UINT_PTR> g_TJSSyncCallOrder;

class TJSContinuousEventHook final : public tTVPContinuousEventCallbackIntf {
 public:
	void TJS_INTF_METHOD OnContinuousCallback(tjs_uint64 tick) override {
		unsigned long long const count =
		    g_TJSContinuousCallbackCount.fetch_add(1, std::memory_order_relaxed) +
		    1;
		g_TJSLastContinuousTick.store(tick, std::memory_order_relaxed);

		// Process sync calls first: they block callers.
		constexpr int kMaxSyncPerCallback = 8;
		for (int processed = 0; processed < kMaxSyncPerCallback; ++processed) {
			auto syncData = TakeNextTJSSyncCall();
			if (!syncData) {
				break;
			}
			ExecuteTJSSyncCall(syncData);
			g_TJSSyncExecutedCount.fetch_add(1, std::memory_order_relaxed);
		}

		// Then async calls.
		constexpr int kMaxAsyncPerCallback = 32;
		for (int processed = 0; processed < kMaxAsyncPerCallback; ++processed) {
			TJSAsyncCall call;
			if (!TakeNextTJSAsyncCall(call)) {
				break;
			}
			ExecuteTJSAsyncCall(call);
			g_TJSAsyncExecutedCount.fetch_add(1, std::memory_order_relaxed);
		}
		if (count == 1 || count % 300 == 0) {
			char message[256];
			sprintf_s(message,
			          "[TJS continuous] callbacks=%llu sync=%llu async=%llu "
			          "dropped=%llu tick=%llu thread=%lu\n",
			          count, g_TJSSyncExecutedCount.load(std::memory_order_relaxed),
			          g_TJSAsyncExecutedCount.load(std::memory_order_relaxed),
			          g_TJSAsyncDroppedCount.load(std::memory_order_relaxed),
			          static_cast<unsigned long long>(tick),
			          static_cast<unsigned long>(GetCurrentThreadId()));
			Log(message);
		}
	}
};

static TJSContinuousEventHook g_TJSContinuousEventHook;

static bool TakeNextTJSAsyncCall(TJSAsyncCall &call) {
	std::lock_guard<std::mutex> lock(g_TJSAsyncCallsMutex);
	while (!g_TJSAsyncCallOrder.empty()) {
		UINT_PTR const id = g_TJSAsyncCallOrder.front();
		g_TJSAsyncCallOrder.pop_front();
		auto it = g_TJSAsyncCalls.find(id);
		if (it == g_TJSAsyncCalls.end()) {
			continue;
		}
		call = std::move(it->second);
		g_TJSAsyncCalls.erase(it);
		return true;
	}
	return false;
}

static void CancelTJSAsyncCalls() {
	std::lock_guard<std::mutex> lock(g_TJSAsyncCallsMutex);
	g_TJSAsyncCalls.clear();
	g_TJSAsyncCallOrder.clear();
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

static std::shared_ptr<TJSSyncCallData> TakeNextTJSSyncCall() {
	std::lock_guard<std::mutex> lock(g_TJSSyncCallsMutex);
	while (!g_TJSSyncCallOrder.empty()) {
		UINT_PTR const id = g_TJSSyncCallOrder.front();
		g_TJSSyncCallOrder.pop_front();
		auto it = g_TJSSyncCalls.find(id);
		if (it == g_TJSSyncCalls.end()) {
			continue;
		}
		// Return a copy: the caller will execute and signal, but the original
		// shared_ptr stays alive in the map until ExecuteTJSSync completes its
		// wait.
		return it->second;
	}
	return nullptr;
}

static void CancelTJSSyncCalls() {
	std::unordered_map<UINT_PTR, std::shared_ptr<TJSSyncCallData>> calls;
	{
		std::lock_guard<std::mutex> lock(g_TJSSyncCallsMutex);
		calls.swap(g_TJSSyncCalls);
		g_TJSSyncCallOrder.clear();
	}
	// Mark cancelled and signal: this unblocks waiting threads that have not yet
	// been picked up by the callback. Requests already taken by the callback are
	// not in the map, so they won't be signaled here; the continuous hook's own
	// removal will ensure the callback stops being invoked.
	for (auto const &entry : calls) {
		entry.second->cancelled.store(true, std::memory_order_release);
		if (entry.second->completedEvent) {
			SetEvent(entry.second->completedEvent);
		}
	}
}

bool IsMainThread() {
	return GetCurrentThreadId() == g_TJSContinuousHookThreadId;
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

static void ExecuteTJSAsyncCall(TJSAsyncCall const &call) {
	if (call.code.empty() || !g_TJSReady.load(std::memory_order_acquire)) {
		return;
	}

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

static void ExecuteTJSSyncCall(std::shared_ptr<TJSSyncCallData> const &data) {
	if (!data) {
		return;
	}

	try {
		if (!data->cancelled.load(std::memory_order_acquire) &&
		    !data->code.empty() && g_TJSReady.load(std::memory_order_acquire)) {
			RunThroughEngine(data->code, &data->result, data->useScriptApi);
			data->executed = true;
		}
	} catch (...) {
		Log("  [ExecuteTJSSync] TJS execution threw an exception\n");
	}
	SetEvent(data->completedEvent);
}

// Defined below, next to the top-level separator scan it builds on.
static bool ResolveUseScriptApi(std::wstring const &code, TJSExecMode mode);

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
	// TJS executor initialization is now handled entirely by the continuous event
	// hook; this function remains as a no-op for API compatibility.
	return true;
}

bool InitializeTJSContinuousEventHook() {
	if (g_TJSContinuousEventHookRegistered.exchange(true,
	                                                std::memory_order_acq_rel)) {
		return true;
	}
	g_TJSContinuousHookThreadId = GetCurrentThreadId();
	g_TJSContinuousCallbackCount.store(0, std::memory_order_relaxed);
	g_TJSLastContinuousTick.store(0, std::memory_order_relaxed);
	g_TJSAsyncExecutedCount.store(0, std::memory_order_relaxed);
	g_TJSAsyncDroppedCount.store(0, std::memory_order_relaxed);
	g_TJSSyncExecutedCount.store(0, std::memory_order_relaxed);
	TVPAddContinuousEventHook(&g_TJSContinuousEventHook);
	g_AcceptTJSCalls.store(true, std::memory_order_release);
	char message[128];
	sprintf_s(message,
	          "TJS continuous event hook registered (async+sync queue), "
	          "registration thread=%lu\n",
	          static_cast<unsigned long>(g_TJSContinuousHookThreadId));
	Log(message);
	return true;
}

constexpr DWORD kShutdownCheckMs = 50;
std::atomic<bool> g_TJSShuttingDown(false);

void BeginTJSExecutorShutdown() {
	g_TJSShuttingDown.store(true, std::memory_order_release);
}

void ShutdownTJSExecutor() {
	StopAcceptingTJSCalls();
	if (g_TJSContinuousEventHookRegistered.exchange(false,
	                                                std::memory_order_acq_rel)) {
		TVPRemoveContinuousEventHook(&g_TJSContinuousEventHook);
		Log("TJS continuous event hook removed\n");
	}
	if (g_ScriptDispatch) {
		g_ScriptDispatch->Release();
		g_ScriptDispatch = nullptr;
	}
	g_TJSReady.store(false);
}

void ExecuteTJSAsync(wchar_t const *code, TJSExecMode mode) {
	if (!g_AcceptTJSCalls.load(std::memory_order_acquire) || !code || !*code) {
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
		if (g_TJSAsyncCalls.size() >= kMaxTJSAsyncCalls) {
			g_TJSAsyncDroppedCount.fetch_add(1, std::memory_order_relaxed);
			Log("  [ExecuteTJSAsync] Queue full; dropping request\n");
			return;
		}
		do {
			callId = g_NextTJSAsyncCallId.fetch_add(1, std::memory_order_relaxed);
		} while (callId == 0 ||
		         g_TJSAsyncCalls.find(callId) != g_TJSAsyncCalls.end());
		g_TJSAsyncCalls.emplace(callId, std::move(call));
		g_TJSAsyncCallOrder.push_back(callId);
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
	if (!g_AcceptTJSCalls.load(std::memory_order_acquire) || !code)
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
		g_TJSSyncCallOrder.push_back(callId);
	}

	// Waited in slices rather than in one block so shutdown is noticed while the
	// game thread is already gone. Waiting the full timeout there added seconds
	// to closing the game and made the window appear to hang.
	DWORD waitResult = WAIT_TIMEOUT;
	for (DWORD waited = 0; waited < timeoutMs; waited += kShutdownCheckMs) {
		DWORD const slice = timeoutMs - waited < kShutdownCheckMs
		                        ? timeoutMs - waited
		                        : kShutdownCheckMs;
		waitResult = WaitForSingleObject(data->completedEvent, slice);
		if (waitResult == WAIT_OBJECT_0) {
			break;
		}
		if (g_TJSShuttingDown.load(std::memory_order_acquire)) {
			break;
		}
	}
	bool completed = waitResult == WAIT_OBJECT_0;
	bool executed = completed && data->executed;
	if (executed && result) {
		*result = data->result;
	}
	if (!completed) {
		data->cancelled.store(true, std::memory_order_release);
		Log("  [ExecuteTJSSync] Timed out waiting for the game thread\n");
	} else if (!executed) {
		// The game thread answered but skipped the call (cancelled, or TJS not
		// ready). This used to return false with no trace at all, which hid the
		// cause of dropped polls from every caller.
		Log("  [ExecuteTJSSync] Game thread skipped the call (not executed)\n");
	}
	// Remove from map now that wait is complete: keeps the shared_ptr alive until
	// after WaitForSingleObject finishes, preventing use-after-free on the event.
	TakeTJSSyncCall(callId);
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
