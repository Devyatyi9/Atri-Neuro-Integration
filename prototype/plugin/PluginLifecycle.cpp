#include "PluginLifecycle.h"

#include <atomic>
#include <thread>

#include "ActionRouter.h"
#include "ContextMonitor.h"
#include "GameDebugLog.h"
#include "Logging.h"
#include "NeuroClient.h"
#include "NeuroConsole.h"
#include "NeuroTCPServer.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"

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

		tTJSVariant result;
		char buf[256];

		TVPExecuteExpression(TJS_W("neuroGetVersion()"), &result);
		sprintf_s(buf, "  neuroGetVersion() returned: %d\n",
		          (int)(tTVInteger)result);
		Log(buf);

		TVPExecuteExpression(TJS_W("neuroIsTJSReady()"), &result);
		sprintf_s(buf, "  neuroIsTJSReady() returned: %d\n",
		          (int)(tTVInteger)result);
		Log(buf);

		Log("Basic tests completed!\n");

		if (tjsDispatcherReady) {
			StartTCPServer();
		}
		StartContextMonitor();
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
	g_InitThread = std::thread(DeferredTJSInitThread);
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
	g_HotkeyThread = std::thread(HotkeyThread);
}

static void StopHotkeyThread() {
	g_HotkeyThreadRunning.store(false);
	if (g_HotkeyThread.joinable()) {
		g_HotkeyThread.join();
	}
}

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter) {
	OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
	Log("\n\n=== V2Link called ===\n");

	if (!exporter) {
		Log("ERROR: exporter is NULL!\n");
		return E_FAIL;
	}

	if (!TVPInitImportStub(exporter)) {
		Log("ERROR: TVPInitImportStub failed!\n");
		return E_FAIL;
	}
	Log("TVPInitImportStub OK\n");

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
	StopContextMonitor();
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
