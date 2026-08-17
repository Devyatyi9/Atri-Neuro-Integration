#include "PluginLifecycle.h"

#include <atomic>
#include <cstdio>
#include <thread>

#include "ContextMonitor.h"
#include "Logging.h"
#include "NeuroClient.h"
#include "NeuroConsole.h"
#include "NeuroTCPServer.h"
#include "SelfLink.h"
#include "TJSBindings.h"
#include "TJSExecutor.h"

#pragma warning(push)
#pragma warning(disable : 26495)  // Uninitialized member variable (tp_stub)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

static std::atomic<bool> g_InitThreadRunning(false);
static std::atomic<bool> g_InitThreadStopRequested(false);
static std::thread g_InitThread;
static std::atomic<bool> g_HotkeyThreadRunning(false);
static std::thread g_HotkeyThread;

static void StartHotkeyThread();

static void DeferredTJSInitThread() {
	LogToFile("Deferred TJS init thread started...\n");

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
			LogToFile(buf);
		}

		if (IsTJSReady())
			break;
	}

	if (!g_InitThreadStopRequested.load() && IsTJSReady()) {
		LogToFile("Deferred TJS init: SUCCESS\n");

		// Subclass the game window for thread-safe TJS calls.
		if (!InitializeTJSExecutor()) {
			LogToFile("WARNING: Window subclass failed, TJS calls may be unsafe\n");
		}

		LogToFile("Registering TJS functions via ncbind...\n");
		RegisterTJSBindings();
		LogToFile("Functions registered via ncbind\n");

		LogToFile("Setting up Debug.addLoggingHandler...\n");
		ExecuteTJSAsync(L"Debug.addLoggingHandler(neuroConsoleLog)");
		LogToFile("Debug logging handler registered!\n");

		LogToFile("Testing registered functions from C++...\n");

		tTJSVariant result;
		char buf[256];

		TVPExecuteExpression(TJS_W("neuroGetVersion()"), &result);
		sprintf_s(buf, "  neuroGetVersion() returned: %d\n",
		          (int)(tTVInteger)result);
		LogToFile(buf);

		TVPExecuteExpression(TJS_W("neuroIsTJSReady()"), &result);
		sprintf_s(buf, "  neuroIsTJSReady() returned: %d\n",
		          (int)(tTVInteger)result);
		LogToFile(buf);

		LogToFile("Basic tests completed!\n");

		StartTCPServer();
		StartContextMonitor();
		StartHotkeyThread();
	} else {
		LogToFile("Deferred TJS init: TIMEOUT (TJS not ready after 10s)\n");
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
	LogToFile("Hotkey thread started (F12 = console)\n");

	while (g_HotkeyThreadRunning.load()) {
		if (GetAsyncKeyState(VK_F12) & 0x8000) {
			ToggleNeuroConsole();
			Sleep(300);
		}

		Sleep(50);
	}

	LogToFile("Hotkey thread stopped\n");
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

extern "C" __declspec(dllexport) bool InitPlugin() {
	LogToFile("\n\n=== InitPlugin called (self-linking) ===\n");
	OutputDebugStringW(L"[NEURO PLUGIN] === InitPlugin called ===\n");

	iTVPFunctionExporter *exporter = WaitForExporterInit(10000);

	if (!exporter) {
		LogToFile("ERROR: Failed to find iTVPFunctionExporter!\n");
		return false;
	}

	HRESULT hr = V2Link(exporter);
	if (FAILED(hr)) {
		LogToFile("ERROR: V2Link failed!\n");
		return false;
	}

	LogToFile("InitPlugin completed successfully\n");
	return true;
}

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter) {
	OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
	LogToFile("\n\n=== V2Link called ===\n");

	if (!exporter) {
		LogToFile("ERROR: exporter is NULL!\n");
		return E_FAIL;
	}

	if (!TVPInitImportStub(exporter)) {
		LogToFile("ERROR: TVPInitImportStub failed!\n");
		return E_FAIL;
	}
	LogToFile("TVPInitImportStub OK\n");

	LogToFile("Starting deferred TJS initialization...\n");
	StartDeferredTJSInit();

	return S_OK;
}

extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink() {
	OutputDebugStringW(L"[NEURO PLUGIN] V2Unlink called - plugin unloading\n");
	LogToFile("V2Unlink called - cleaning up\n");

	StopDeferredTJSInit();
	StopHotkeyThread();
	StopContextMonitor();
	StopTCPServer();
	NeuroDisconnect();
	ShutdownNeuroConsole();

	tTJSVariant dummy;
	TVPExecuteExpression(TJS_W("Debug.removeLoggingHandler(neuroConsoleLog)"),
	                     &dummy);

	UnregisterTJSBindings();
	ShutdownTJSExecutor();
	TVPUninitImportStub();

	LogToFile("Cleanup complete\n");
	CloseLogFile();

	return S_OK;
}
