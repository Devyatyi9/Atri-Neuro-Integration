#include "PluginLifecycle.h"

#include <atomic>
#include <cstdio>  // IWYU pragma: keep -- declares sprintf_s
#include <exception>
#include <string>
#include <thread>

#include "Logging.h"
#include "NeuroConsole.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"
#include "ThreadGuard.h"

#pragma warning(push)
#pragma warning(disable : 26495)  // Uninitialized member variable (tp_stub)
#include "tp_stub.h"
#pragma warning(pop)

static std::atomic<bool> g_InitThreadRunning(false);
static std::atomic<bool> g_InitThreadStopRequested(false);
static std::thread g_InitThread;
static std::atomic<bool> g_HotkeyThreadRunning(false);
static std::thread g_HotkeyThread;
static std::atomic<bool> g_WorkerShutdownStarted(false);
static std::atomic<bool> g_ShutdownStarted(false);

static void StartHotkeyThread();

static void DeferredTJSInitThread() {
	Log("Deferred TJS init thread started\n");

	int const maxAttempts = 100;
	int const intervalMs = 100;

	for (int i = 0; i < maxAttempts; i++) {
		Sleep(intervalMs);
		if (g_InitThreadStopRequested.load()) {
			break;
		}
		TryInitTJS();

		if (IsTJSReady())
			break;
	}

	if (g_InitThreadStopRequested.load() || !IsTJSReady()) {
		Log("Deferred TJS init: TJS never became ready\n");
		g_InitThreadRunning.store(false);
		return;
	}
	Log("Deferred TJS init: TJS ready\n");

	// The console runs off the main thread and reaches TJS through this
	// dispatcher.
	if (!InitializeTJSExecutor()) {
		Log("ERROR: window subclass failed; TJS dispatch disabled\n");
	}

	RegisterTJSBindings();
	Log("TJS functions registered via ncbind\n");

	// Keep the registered callback in TJS so it cannot reference an unloaded
	// DLL.
	ExecuteTJSAsync(
	    L"(function(){ var installed = false; "
	    L"try { installed = Debug.__neuroLoggingHandlerInstalled; } catch(e) "
	    L"{} "
	    L"if (!installed) { "
	    L"Debug.__neuroLoggingHandler = function(msg) { "
	    L"try { neuroConsoleLog(msg); } catch(e) {} }; "
	    L"Debug.addLoggingHandler(Debug.__neuroLoggingHandler); "
	    L"Debug.__neuroLoggingHandlerInstalled = true; } })()",
	    TJSExecMode::Expression);

	StartHotkeyThread();
	Log("Hotkey thread started (F12 = console)\n");

	g_InitThreadRunning.store(false);
}

static void StartDeferredTJSInit() {
	if (g_InitThreadRunning.exchange(true)) {
		return;
	}
	if (g_InitThread.joinable()) {
		g_InitThread.join();
	}
	g_InitThreadStopRequested.store(false);
	g_InitThread = std::thread(
	    [] { RunThreadGuarded("deferred init", DeferredTJSInitThread); });
}

static void StopDeferredTJSInit() {
	g_InitThreadStopRequested.store(true);
	if (g_InitThread.joinable()) {
		g_InitThread.join();
	}
}

// GetAsyncKeyState reads the keyboard, not the window: it says nothing about
// who has focus. With one instance that was invisible, but two copies of the
// game both answered the same F12 and each opened its own console -- so the
// console that appeared was whichever won the race, not the one belonging to
// the window being used, and sounds played from it went to the wrong process.
static bool ThisProcessHasFocus() {
	HWND const foreground = GetForegroundWindow();
	if (!foreground) {
		return false;
	}
	DWORD processId = 0;
	GetWindowThreadProcessId(foreground, &processId);
	return processId == GetCurrentProcessId();
}

static void HotkeyThread() {
	while (g_HotkeyThreadRunning.load()) {
		// The console window belongs to this process too, so it keeps its own
		// F12 working while it is focused.
		if ((GetAsyncKeyState(VK_F12) & 0x8000) && ThisProcessHasFocus()) {
			ToggleNeuroConsole();
			Sleep(300);
		}

		Sleep(50);
	}
}

static void StartHotkeyThread() {
	if (g_HotkeyThreadRunning.exchange(true)) {
		return;
	}
	g_HotkeyThread =
	    std::thread([] { RunThreadGuarded("hotkey", HotkeyThread); });
}

static void StopHotkeyThread() {
	g_HotkeyThreadRunning.store(false);
	if (g_HotkeyThread.joinable()) {
		g_HotkeyThread.join();
	}
}

// The process can die without leaving a word, and twice now it has: procdump
// recorded `Unhandled: C0000409` -- a fail-fast from terminate() -- and could
// not write a dump because the process was gone before it got there. These two
// handlers cost nothing and turn that silence into a line a debugger can see.
//
// std::terminate is where an unhandled C++ exception ends up, including one
// thrown on the game's own main thread, which no thread guard of ours covers.
// The unhandled-exception filter catches the other family, the access
// violations, and names the address.
static LPTOP_LEVEL_EXCEPTION_FILTER g_PreviousExceptionFilter = nullptr;

static void OnTerminate() {
	// Rethrowing is the only way to see what it was: terminate is called with
	// the exception still in flight.
	std::string detail = "no exception in flight";
	if (std::exception_ptr const current = std::current_exception()) {
		try {
			std::rethrow_exception(current);
		} catch (std::exception const &error) {
			detail = std::string("std::exception: ") + error.what();
		} catch (...) {
			detail = "a non-standard exception";
		}
	}
	OutputDebugStringA(
	    ("[NEURO PLUGIN] terminate called -- " + detail + "\n").c_str());
	// Not abort(): in a debug CRT that raises its own modal dialog, and the one
	// time this fired it stacked on top of the engine's error box on a window
	// that was already not answering -- there was no way left to dismiss either.
	// The line is written; ending quietly is the whole remaining job.
	TerminateProcess(GetCurrentProcess(), 0xC0000409);
}

static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS *info) {
	char line[192] = {};
	if (info && info->ExceptionRecord) {
		sprintf_s(line, "[NEURO PLUGIN] unhandled exception 0x%08lX at 0x%p\n",
		          info->ExceptionRecord->ExceptionCode,
		          info->ExceptionRecord->ExceptionAddress);
	} else {
		sprintf_s(line, "[NEURO PLUGIN] unhandled exception, no record\n");
	}
	OutputDebugStringA(line);
	return g_PreviousExceptionFilter ? g_PreviousExceptionFilter(info)
	                                 : EXCEPTION_CONTINUE_SEARCH;
}

static void InstallFatalHandlers() {
	static bool installed = false;
	if (installed) {
		return;
	}
	installed = true;
	std::set_terminate(OnTerminate);
	g_PreviousExceptionFilter = SetUnhandledExceptionFilter(OnUnhandledException);
}

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter) {
	OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
	Log("=== V2Link called ===\n");
	InstallFatalHandlers();

	if (!exporter) {
		Log("ERROR: exporter is NULL\n");
		return E_FAIL;
	}

	if (!TVPInitImportStub(exporter)) {
		Log("ERROR: TVPInitImportStub failed\n");
		return E_FAIL;
	}
	if (!InitializeTJSContinuousEventHook()) {
		Log("ERROR: TJS continuous event hook registration failed\n");
	}

	StartDeferredTJSInit();

	return S_OK;
}

void ShutdownPluginWorkers() {
	// Before anything is joined: the workers are mostly blocked in synchronous
	// TJS calls, and the game thread that answers them is on its way out.
	BeginTJSExecutorShutdown();
	if (g_WorkerShutdownStarted.exchange(true)) {
		return;
	}

	Log("Plugin worker shutdown started\n");

	// Release workers blocked on main-thread TJS calls before joining them.
	StopAcceptingTJSCalls();
	StopDeferredTJSInit();
	StopHotkeyThread();
	ShutdownNeuroConsole();
}

void ShutdownPluginLifecycle() {
	if (g_ShutdownStarted.exchange(true)) {
		return;
	}

	ShutdownPluginWorkers();

	UnregisterTJSBindings();
	ShutdownTJSExecutor();
	TVPUninitImportStub();
	Log("Cleanup complete\n");
}

extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink() {
	OutputDebugStringW(L"[NEURO PLUGIN] V2Unlink called - plugin unloading\n");
	ShutdownPluginLifecycle();

	return S_OK;
}
