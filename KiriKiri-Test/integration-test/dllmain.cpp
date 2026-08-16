#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
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

#define WIN32_LEAN_AND_MEAN

// ============================================================================
// KiriKiri SDK - tp_stub (для консоли и TVPExecuteExpression)
// ============================================================================
#pragma warning(push)
#pragma warning(disable : 26495)  // Uninitialized member variable (tp_stub)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

// ============================================================================
// Глобальные переменные для отложенной инициализации
// ============================================================================
static std::atomic<bool> g_InitThreadRunning(false);
static std::atomic<bool> g_HotkeyThreadRunning(false);
static std::thread g_HotkeyThread;  // Для корректного join()

void StartHotkeyThread();

void DeferredTJSInitThread() {
	LogToFile("Deferred TJS init thread started...\n");

	int const maxAttempts = 100;  // 10 секунд максимум
	int const intervalMs = 100;

	for (int i = 0; i < maxAttempts; i++) {
		Sleep(intervalMs);
		TryInitTJS();

		// Логируем каждую секунду
		if ((i + 1) % 10 == 0) {
			char buf[64];
			sprintf_s(buf, "  Waiting for TJS... attempt %d/%d\n", i + 1,
			          maxAttempts);
			LogToFile(buf);
		}

		if (IsTJSReady())
			break;
	}

	if (IsTJSReady()) {
		LogToFile("Deferred TJS init: SUCCESS\n");

		// Subclass окна игры для thread-safe TJS вызовов
		if (!InitializeTJSExecutor()) {
			LogToFile("WARNING: Window subclass failed, TJS calls may be unsafe\n");
		}

		// Регистрируем функции в TJS через ncbind
		LogToFile("Registering TJS functions via ncbind...\n");
		RegisterTJSBindings();
		LogToFile("Functions registered via ncbind\n");

		// Регистрируем перехват Debug.message -> NeuroConsole
		LogToFile("Setting up Debug.addLoggingHandler...\n");
		ExecuteTJSAsync(L"Debug.addLoggingHandler(neuroConsoleLog)");
		LogToFile("Debug logging handler registered!\n");

		// Тестируем вызов зарегистрированных функций из C++
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

		// Запускаем TCP сервер для удалённого выполнения TJS
		StartTCPServer();

		// Запускаем монитор контекста (опрос изменений)
		StartContextMonitor();

		// Запускаем обработчик хоткеев
		StartHotkeyThread();
	} else {
		LogToFile("Deferred TJS init: TIMEOUT (TJS not ready after 10s)\n");
	}

	g_InitThreadRunning.store(false);
}

void StartDeferredTJSInit() {
	if (g_InitThreadRunning.exchange(true)) {
		return;  // Уже запущен
	}
	std::thread(DeferredTJSInitThread).detach();
}

// ============================================================================
// Хоткеи (thread-safe через Window Subclass)
// ============================================================================

void HotkeyThread() {
	LogToFile("Hotkey thread started (F12 = console)\n");

	while (g_HotkeyThreadRunning.load()) {
		// F12 - показать/скрыть Neuro Console
		if (GetAsyncKeyState(VK_F12) & 0x8000) {
			// LogToFile("F12 pressed - toggling Neuro Console\n");
			ToggleNeuroConsole();
			Sleep(300);  // Debounce
		}

		Sleep(50);  // Не грузим CPU
	}

	LogToFile("Hotkey thread stopped\n");
}

void StartHotkeyThread() {
	if (g_HotkeyThreadRunning.exchange(true)) {
		return;
	}
	g_HotkeyThread = std::thread(HotkeyThread);
}

void StopHotkeyThread() {
	g_HotkeyThreadRunning.store(false);
	if (g_HotkeyThread.joinable()) {
		g_HotkeyThread.join();  // Ждём корректного завершения
	}
}

// ============================================================================
// InitPlugin - точка входа для self-linking
// Вызывается из proxy DLL вместо прямого V2Link
// ============================================================================

// Forward declaration
extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter);

extern "C" __declspec(dllexport) bool InitPlugin() {
	LogToFile("\n\n=== InitPlugin called (self-linking) ===\n");
	OutputDebugStringW(L"[NEURO PLUGIN] === InitPlugin called ===\n");

	// Ищем iTVPFunctionExporter в памяти игры
	iTVPFunctionExporter *exporter = WaitForExporterInit(10000);

	if (!exporter) {
		LogToFile("ERROR: Failed to find iTVPFunctionExporter!\n");
		return false;
	}

	// Вызываем V2Link для инициализации
	HRESULT hr = V2Link(exporter);
	if (FAILED(hr)) {
		LogToFile("ERROR: V2Link failed!\n");
		return false;
	}

	LogToFile("InitPlugin completed successfully\n");
	return true;
}

// ============================================================================
// V2Link - регистрация плагина (теперь вызывается из InitPlugin)
// ============================================================================

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(
    iTVPFunctionExporter *exporter) {
	OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
	LogToFile("\n\n=== V2Link called ===\n");

	// RegisterTJSBindings() будет вызван в DeferredTJSInitThread после готовности
	// TJS
	if (!exporter) {
		LogToFile("ERROR: exporter is NULL!\n");
		return E_FAIL;
	}

	if (!TVPInitImportStub(exporter)) {
		LogToFile("ERROR: TVPInitImportStub failed!\n");
		return E_FAIL;
	}
	LogToFile("TVPInitImportStub OK\n");

	// Запускаем отложенную инициализацию TJS в фоне
	LogToFile("Starting deferred TJS initialization...\n");
	StartDeferredTJSInit();

	return S_OK;
}

// ============================================================================
// V2Unlink - выгрузка плагина
// ============================================================================

extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink() {
	OutputDebugStringW(L"[NEURO PLUGIN] V2Unlink called - plugin unloading\n");
	LogToFile("V2Unlink called - cleaning up\n");

	// Останавливаем хоткей поток (join ждёт завершения)
	StopHotkeyThread();

	// Останавливаем монитор контекста
	StopContextMonitor();

	// Останавливаем TCP сервер
	StopTCPServer();

	// Stop the Neuro transport before tearing down TJS and plugin state.
	NeuroDisconnect();

	// Закрываем консоль
	ShutdownNeuroConsole();

	// Удаляем logging handler
	tTJSVariant dummy;
	TVPExecuteExpression(TJS_W("Debug.removeLoggingHandler(neuroConsoleLog)"),
	                     &dummy);

	// Отменяем регистрацию функций через ncbind
	UnregisterTJSBindings();

	// Stop TJS marshalling and release the script dispatch after TJS cleanup.
	ShutdownTJSExecutor();

	// Освобождаем tp_stub
	TVPUninitImportStub();

	LogToFile("Cleanup complete\n");
	// Закрываем файл лога
	CloseLogFile();

	return S_OK;
}

// ============================================================================
// DllMain
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hModule);
		OutputDebugStringW(L"[NEURO PLUGIN] DLL loaded\n");
		LogToFile("\n\n=== NEURO PLUGIN SESSION STARTED ===\n");
	} else if (reason == DLL_PROCESS_DETACH) {
		OutputDebugStringW(L"[NEURO PLUGIN] DLL unloading\n");
		LogToFile("=== PLUGIN SESSION ENDED ===\n");
	}
	return TRUE;
}
