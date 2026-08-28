#include "PluginLifecycle.h"

#include <atomic>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>

#include "ActionRouter.h"
#include "GameDebugLog.h"
#include "Logging.h"
#include "NeuroClient.h"
#include "NeuroConsole.h"
#include "NeuroTCPServer.h"
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
	Log("Deferred TJS init thread started...\n");

	int const maxAttempts = 100;
	int const intervalMs = 100;

	for (int i = 0; i < maxAttempts; i++) {
		Sleep(intervalMs);
		if (g_InitThreadStopRequested.load()) {
			break;
		}
		TryInitTJS();

		if ((i + 1) % 10 == 0) {
			char buf[64];
			sprintf_s(buf, "  Waiting for TJS... attempt %d/%d\n", i + 1,
			          maxAttempts);
			Log(buf);
		}

		if (IsTJSReady())
			break;
	}

	if (!g_InitThreadStopRequested.load() && IsTJSReady()) {
		Log("Deferred TJS init: SUCCESS\n");

		// The TCP console depends on this main-thread dispatcher.
		bool const tjsDispatcherReady = InitializeTJSExecutor();
		if (!tjsDispatcherReady) {
			Log("ERROR: Window subclass failed; TCP console disabled\n");
		}

		Log("Registering TJS functions via ncbind...\n");
		RegisterTJSBindings();
		Log("Functions registered via ncbind\n");
		InitializePrototypeActionRouter();

		Log("Setting up Debug.addLoggingHandler...\n");
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
		Log("Debug logging handler registered!\n");

		Log("Testing registered functions from C++...\n");

		std::string result;
		char buf[256];

		if (ExecuteTJSSync(L"neuroGetVersion()", &result, 5000)) {
			sprintf_s(buf, "  neuroGetVersion() returned: %s\n", result.c_str());
			Log(buf);
		} else {
			Log("  ERROR: neuroGetVersion() timed out\n");
		}

		if (ExecuteTJSSync(L"neuroIsTJSReady()", &result, 5000)) {
			sprintf_s(buf, "  neuroIsTJSReady() returned: %s\n", result.c_str());
			Log(buf);
		} else {
			Log("  ERROR: neuroIsTJSReady() timed out\n");
		}

		Log("Basic tests completed!\n");

		if (tjsDispatcherReady) {
			StartTCPServer();
		}

		// Install event-driven context monitor hook instead of polling thread.
		// The hook is called by conductor.onTag after each tag is processed,
		// ensuring safe state for context monitoring without race conditions.
		Log("Installing context monitor hook via conductor.onTag...\n");
		ExecuteTJSAsync(
		    L"neuroExecFile('neuro_scripts/install_context_hook.tjs', 1)",
		    TJSExecMode::Expression);

		StartHotkeyThread();
		NeuroAutoConnect();
	} else {
		Log("Deferred TJS init: TIMEOUT (TJS not ready after 10s)\n");
	}

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

static void HotkeyThread() {
	Log("Hotkey thread started (F12 = console)\n");

	while (g_HotkeyThreadRunning.load()) {
		if (GetAsyncKeyState(VK_F12) & 0x8000) {
			ToggleNeuroConsole();
			Sleep(300);
		}

		Sleep(50);
	}

	Log("Hotkey thread stopped\n");
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
// handlers cost nothing and turn that silence into a line.
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
	Log(("[Fatal] terminate called -- " + detail + "\n").c_str());
	FlushLogFileIfFree();
	// Not abort(): in a debug CRT that raises its own modal dialog, and the one
	// time this fired it stacked on top of the engine's error box on a window
	// that was already not answering -- there was no way left to dismiss either.
	// The line is written; ending quietly is the whole remaining job.
	TerminateProcess(GetCurrentProcess(), 0xC0000409);
}

static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS *info) {
	char line[192] = {};
	if (info && info->ExceptionRecord) {
		sprintf_s(line, "[Fatal] unhandled exception 0x%08lX at 0x%p\n",
		          info->ExceptionRecord->ExceptionCode,
		          info->ExceptionRecord->ExceptionAddress);
	} else {
		sprintf_s(line, "[Fatal] unhandled exception, no record\n");
	}
	Log(line);
	FlushLogFileIfFree();
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
	Log("Fatal handlers installed\n");
}

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter) {
	OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
	Log("\n\n=== V2Link called ===\n");
	InstallFatalHandlers();

	if (!exporter) {
		Log("ERROR: exporter is NULL!\n");
		return E_FAIL;
	}

	if (!TVPInitImportStub(exporter)) {
		Log("ERROR: TVPInitImportStub failed!\n");
		return E_FAIL;
	}
	Log("TVPInitImportStub OK\n");
	if (!InitializeTJSContinuousEventHook()) {
		Log("ERROR: TJS continuous event hook registration failed\n");
	}

	Log("Starting deferred TJS initialization...\n");
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

	ShutdownPrototypeActionRouter();

	// Release workers blocked on main-thread TJS calls before joining them.
	StopAcceptingTJSCalls();
	StopDeferredTJSInit();
	StopHotkeyThread();
	NeuroDisconnect();
	ShutdownNeuroConsole();
	StopTCPServer();
	CloseGameDebugLogFile();
}

void ShutdownPluginLifecycle() {
	if (g_ShutdownStarted.exchange(true)) {
		return;
	}

	Log("Plugin lifecycle shutdown started\n");
	ShutdownPluginWorkers();

	UnregisterTJSBindings();
	ShutdownTJSExecutor();
	TVPUninitImportStub();

	Log("Cleanup complete\n");
	CloseTagLogFile();
	CloseLogFile();
}

extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink() {
	OutputDebugStringW(L"[NEURO PLUGIN] V2Unlink called - plugin unloading\n");
	Log("V2Unlink called - cleaning up\n");
	ShutdownPluginLifecycle();

	return S_OK;
}
