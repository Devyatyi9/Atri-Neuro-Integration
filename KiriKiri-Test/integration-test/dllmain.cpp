#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../include/neurosdk.h"
#include "ContextMonitor.h"
#include "Logging.h"
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

// ============================================================================
// Neuro SDK - глобальные переменные
// ============================================================================
static neurosdk_context_t g_NeuroContext = nullptr;
static std::atomic<bool> g_NeuroConnected(false);
static std::atomic<bool> g_NeuroPollThreadRunning(false);
static std::thread g_NeuroPollThread;
static std::string g_NeuroGameName = "ATRI-MyDearMoments";
static std::string g_NeuroURL = "ws://localhost:8000";

// Registered actions (для отслеживания)
static std::vector<std::string> g_RegisteredActions;
static std::mutex g_ActionsMutex;

// ============================================================================
// Neuro SDK - Callback и вспомогательные функции
// ============================================================================
#pragma region Neuro
// Callback для логов от Neuro SDK
void NeuroLogCallback(neurosdk_severity_e severity,
                      char *message,
                      void *user_data) {
	char const *severityStr = "???";
	switch (severity) {
		case NeuroSDK_Severity_Debug:
			severityStr = "DEBUG";
			break;
		case NeuroSDK_Severity_Info:
			severityStr = "INFO";
			break;
		case NeuroSDK_Severity_Warn:
			severityStr = "WARN";
			break;
		case NeuroSDK_Severity_Error:
			severityStr = "ERROR";
			break;
	}

	char buf[1024];
	sprintf_s(buf, "[NeuroSDK %s] %s\n", severityStr,
	          message ? message : "(null)");
	LogToFile(buf);
}
#pragma endregion
#pragma region Neuro2
// Обработка входящего Action от Neuro
void HandleNeuroAction(char const *id, char const *name, char const *data) {
	char logBuf[512];
	sprintf_s(logBuf, "[Neuro] Action received: id=%s, name=%s\n",
	          id ? id : "(null)", name ? name : "(null)");
	LogToFile(logBuf);

	if (data) {
		sprintf_s(logBuf, "[Neuro] Action data: %s\n", data);
		LogToFile(logBuf);
	}

	// Формируем вызов TJS обработчика: neuroOnAction(id, name, data)
	// Экранируем строки для TJS
	wchar_t tjsCode[2048];

	// Простой вызов глобальной функции если она существует
	swprintf_s(tjsCode, 2048,
	           L"if (typeof global.neuroOnAction !== 'undefined') { "
	           L"global.neuroOnAction('%hs', '%hs', '%hs'); "
	           L"}",
	           id ? id : "", name ? name : "", data ? data : "");

	// Асинхронный вызов в главном потоке игры
	ExecuteTJSAsync(tjsCode);
}

// Polling thread для Neuro SDK
void NeuroPollThread() {
	LogToFile("[Neuro] Poll thread started\n");

	while (g_NeuroPollThreadRunning.load() && g_NeuroConnected.load()) {
		if (!neurosdk_context_connected(&g_NeuroContext)) {
			LogToFile("[Neuro] Connection lost!\n");
			g_NeuroConnected.store(false);
			break;
		}

		neurosdk_message_t *messages = nullptr;
		int count = 0;

		neurosdk_error_e err =
		    neurosdk_context_poll(&g_NeuroContext, &messages, &count);

		if (err != NeuroSDK_None) {
			char buf[256];
			sprintf_s(buf, "[Neuro] Poll error: %s\n", neurosdk_error_string(err));
			LogToFile(buf);

			// Некоторые ошибки не фатальны
			if (err == NeuroSDK_UnknownCommand) {
				Sleep(100);
				continue;
			}
			break;
		}

		// Обрабатываем полученные сообщения
		for (int i = 0; i < count; i++) {
			neurosdk_message_t *msg = &messages[i];

			if (msg->kind == NeuroSDK_MessageKind_Action) {
				HandleNeuroAction(msg->value.action.id, msg->value.action.name,
				                  msg->value.action.data);
			}

			neurosdk_message_destroy(msg);
		}

		Sleep(50);  // poll interval
	}

	LogToFile("[Neuro] Poll thread stopped\n");
}

// Подключение к Neuro
bool NeuroConnect(char const *url, char const *gameName) {
	if (g_NeuroConnected.load()) {
		LogToFile("[Neuro] Already connected\n");
		return true;
	}

	if (url)
		g_NeuroURL = url;
	if (gameName)
		g_NeuroGameName = gameName;

	char buf[512];
	sprintf_s(buf, "[Neuro] Connecting to %s as '%s'...\n", g_NeuroURL.c_str(),
	          g_NeuroGameName.c_str());
	LogToFile(buf);

	neurosdk_context_create_desc_t desc;
	memset(&desc, 0, sizeof(desc));
	desc.url = g_NeuroURL.c_str();
	desc.game_name = g_NeuroGameName.c_str();
	desc.poll_ms = 100;
	desc.user_data = nullptr;
	desc.callback_log = NeuroLogCallback;
	desc.flags = NeuroSDK_ContextCreateFlags_None;

	neurosdk_error_e err = neurosdk_context_create(&g_NeuroContext, &desc);
	if (err != NeuroSDK_None) {
		sprintf_s(buf, "[Neuro] Connection failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	g_NeuroConnected.store(true);
	LogToFile("[Neuro] Connected successfully!\n");

	// Запускаем polling thread
	g_NeuroPollThreadRunning.store(true);
	g_NeuroPollThread = std::thread(NeuroPollThread);

	return true;
}

// Отключение от Neuro
void NeuroDisconnect() {
	if (!g_NeuroConnected.load()) {
		return;
	}

	LogToFile("[Neuro] Disconnecting...\n");

	// Останавливаем polling thread
	g_NeuroPollThreadRunning.store(false);
	g_NeuroConnected.store(false);

	if (g_NeuroPollThread.joinable()) {
		g_NeuroPollThread.join();
	}

	// Уничтожаем контекст
	if (g_NeuroContext) {
		neurosdk_context_destroy(&g_NeuroContext);
		g_NeuroContext = nullptr;
	}

	// Очищаем список actions
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		g_RegisteredActions.clear();
	}

	LogToFile("[Neuro] Disconnected\n");
}

// Отправка контекста в Neuro
bool NeuroSendContext(char const *message, bool silent) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot send context: not connected\n");
		return false;
	}

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_Context;
	msg.value.context.message = const_cast<char *>(message);
	msg.value.context.silent = silent;

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Send context failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	char buf[512];
	sprintf_s(buf, "[Neuro] Context sent: %s\n", message);
	LogToFile(buf);
	return true;
}

// Регистрация action в Neuro
bool NeuroRegisterAction(char const *name,
                         char const *description,
                         char const *jsonSchema) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot register action: not connected\n");
		return false;
	}

	neurosdk_action_t action;
	action.name = const_cast<char *>(name);
	action.description = const_cast<char *>(description);
	action.json_schema = const_cast<char *>(jsonSchema ? jsonSchema : "{}");

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_ActionsRegister;
	msg.value.actions_register.actions = &action;
	msg.value.actions_register.actions_len = 1;

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Register action failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	// Сохраняем в список
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		g_RegisteredActions.push_back(name);
	}

	char buf[256];
	sprintf_s(buf, "[Neuro] Action registered: %s\n", name);
	LogToFile(buf);
	return true;
}

// Отправка результата action
bool NeuroSendActionResult(char const *id, bool success, char const *message) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot send result: not connected\n");
		return false;
	}

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_ActionResult;
	msg.value.action_result.id = const_cast<char *>(id);
	msg.value.action_result.success = success;
	msg.value.action_result.message = const_cast<char *>(message ? message : "");

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Send result failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	char buf[256];
	sprintf_s(buf, "[Neuro] Result sent for %s: %s\n", id,
	          success ? "success" : "failure");
	LogToFile(buf);
	return true;
}

// Force action (попросить Neuro выполнить действие)
bool NeuroForceAction(char const *state,
                      char const *query,
                      char const **actionNames,
                      int actionCount,
                      bool ephemeral) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot force action: not connected\n");
		return false;
	}

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_ActionsForce;
	msg.value.actions_force.state = const_cast<char *>(state ? state : "");
	msg.value.actions_force.query = const_cast<char *>(query ? query : "");
	msg.value.actions_force.ephemeral_context = ephemeral;
	msg.value.actions_force.action_names = const_cast<char **>(actionNames);
	msg.value.actions_force.action_names_len = actionCount;
	msg.value.actions_force.priority = NeuroSDK_Priority_Medium;

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Force action failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	LogToFile("[Neuro] Force action sent\n");
	return true;
}
#pragma endregion

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
