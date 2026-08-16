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
#include "TJSExecutor.h"
#include "ncbind.hpp"

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
// Forward declarations
bool RegisterAllFunctions();
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
		ncbAutoRegister::AllRegist();
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
// TJS функции (простые обёртки для регистрации через exporter)
// ============================================================================

static tjs_error TJS_INTF_METHOD NeuroTestFunction(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroTestFunction called from TJS!\n");
	LogToFile(">>> NeuroTestFunction CALLED FROM TJS SCRIPT!\n");

	MessageBoxW(NULL,
	            L"SUCCESS!\n\n"
	            L"NeuroTestFunction was successfully called from TJS!\n\n"
	            L"Your Neuro-integration plugin is fully working!",
	            L"Neuro Plugin - Function Called!", MB_OK | MB_ICONINFORMATION);

	if (result) {
		*result = TJS_W("Hello from Neuro-integration plugin!");
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroGetVersion(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroGetVersion called!\n");
	LogToFile(">>> NeuroGetVersion called\n");

	if (result) {
		*result = (tTVInteger)100;  // Version 1.0.0
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroEcho(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	OutputDebugStringW(L"[NEURO PLUGIN] NeuroEcho called!\n");
	LogToFile(">>> NeuroEcho called\n");

	if (numparams > 0 && param && param[0]) {
		tTJSVariant *firstParam = param[0];

		if (firstParam->Type() == tvtString) {
			ttstr str = *firstParam;
			MessageBoxW(NULL, str.c_str(), L"Neuro Echo", MB_OK | MB_ICONINFORMATION);

			if (result) {
				*result = *firstParam;
			}
		} else if (firstParam->Type() == tvtInteger) {
			wchar_t buffer[128];
			wsprintf(buffer, L"Number received: %I64d", (tTVInteger)*firstParam);
			MessageBoxW(NULL, buffer, L"Neuro Echo", MB_OK | MB_ICONINFORMATION);

			if (result) {
				*result = *firstParam;
			}
		}
	}

	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroSendData(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
	LogToFile(">>> NeuroSendData called with ");

	char logMsg[128];
	sprintf_s(logMsg, sizeof(logMsg), "%d parameters\n", numparams);
	LogToFile(logMsg);

	// TODO: Здесь будет интеграция с libneurosdk

	if (result) {
		*result = (tTVInteger)1;  // Успешно
	}

	return TJS_S_OK;
}

// Показать встроенную консоль Debug
static tjs_error TJS_INTF_METHOD NeuroShowConsole(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	LogToFile(">>> NeuroShowConsole called\n");

	tTJSVariant checkResult;
	TVPExecuteExpression(
	    TJS_W(
	        "typeof Debug != 'undefined' && typeof Debug.console != 'undefined'"),
	    &checkResult);

	if ((tTVInteger)checkResult != 0) {
		tTJSVariant dummy;
		TVPExecuteExpression(TJS_W("Debug.console.visible = true"), &dummy);
		LogToFile("  Debug.console.visible = true\n");
		if (result)
			*result = (tTVInteger)1;
	} else {
		LogToFile("  Debug.console not available\n");
		if (result)
			*result = (tTVInteger)0;
	}

	return TJS_S_OK;
}

// Проверка готовности KAG
static tjs_error TJS_INTF_METHOD NeuroIsKAGReady(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	tTJSVariant checkResult;
	TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &checkResult);
	bool ready = (tTVInteger)checkResult != 0;

	LogToFile(ready ? ">>> NeuroIsKAGReady: YES\n" : ">>> NeuroIsKAGReady: NO\n");

	if (result)
		*result = (tTVInteger)(ready ? 1 : 0);
	return TJS_S_OK;
}

// Проверка готовности TJS (для отладки)
static tjs_error TJS_INTF_METHOD NeuroIsTJSReady(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	bool ready = IsTJSReady();
	LogToFile(ready ? ">>> NeuroIsTJSReady: YES\n" : ">>> NeuroIsTJSReady: NO\n");

	if (result) {
		*result = (tTVInteger)(ready ? 1 : 0);
	}

	return TJS_S_OK;
}

// Добавить текст в NeuroConsole (для перехвата Debug.message)
static tjs_error TJS_INTF_METHOD NeuroConsoleLog(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		return TJS_S_OK;  // Просто игнорируем если нет параметра
	}

	ttstr line = *param[0];
	if (!line.IsEmpty()) {
		// Добавляем перевод строки
		ttstr lineWithNL = line + TJS_W("\n");
		AppendToNeuroConsole(lineWithNL.c_str(), RGB(100, 100, 100));
	}

	return TJS_S_OK;
}

// Класс для перечисления членов объекта (для EnumMembers)
class tEnumMemberCallback : public tTJSDispatch {
 public:
	int m_count;
	int m_maxCount;

	tEnumMemberCallback(int maxCount = 50) : m_count(0), m_maxCount(maxCount) { }

	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag,
	                                   tjs_char const *membername,
	                                   tjs_uint32 *hint,
	                                   tTJSVariant *result,
	                                   tjs_int numparams,
	                                   tTJSVariant **param,
	                                   iTJSDispatch2 *objthis) override {
		// EnumMembers вызывает callback(name, flags, value)
		if (numparams < 3)
			return TJS_S_OK;

		ttstr name = *param[0];
		// param[1] = flags
		tTJSVariant *value = param[2];

		if (name.IsEmpty())
			return TJS_S_OK;

		if (m_count >= m_maxCount) {
			if (m_count == m_maxCount) {
				wchar_t buf[64];
				swprintf_s(buf, L"  ... (more than %d members)\n", m_maxCount);
				AppendToNeuroConsole(buf, RGB(128, 128, 128));
			}
			m_count++;
			return TJS_S_OK;
		}
		m_count++;

		// Имя члена
		AppendToNeuroConsole(L"  ", RGB(0, 0, 0));
		AppendToNeuroConsole(name.c_str(), RGB(0, 128, 128));
		AppendToNeuroConsole(L": ", RGB(0, 0, 0));

		// Тип и значение
		if (!value) {
			AppendToNeuroConsole(L"(no value)\n", RGB(128, 128, 128));
			return TJS_S_OK;
		}

		tTJSVariantType vtype = value->Type();
		wchar_t buf[128];

		switch (vtype) {
			case tvtVoid:
				AppendToNeuroConsole(L"void\n", RGB(128, 128, 128));
				break;
			case tvtObject: {
				iTJSDispatch2 *memberObj = value->AsObjectNoAddRef();
				if (memberObj) {
					tTJSVariant memberClass;
					if (TJS_SUCCEEDED(
					        memberObj->ClassInstanceInfo(TJS_CII_GET, 0, &memberClass))) {
						ttstr mcStr = memberClass;
						AppendToNeuroConsole(L"[", RGB(0, 0, 255));
						AppendToNeuroConsole(mcStr.c_str(), RGB(128, 0, 128));
						AppendToNeuroConsole(L"]\n", RGB(0, 0, 255));
					} else {
						AppendToNeuroConsole(L"[Object]\n", RGB(0, 0, 255));
					}
				} else {
					AppendToNeuroConsole(L"null\n", RGB(128, 128, 128));
				}
				break;
			}
			case tvtString: {
				ttstr str = *value;
				// Обрезаем длинные строки
				if (str.GetLen() > 50) {
					str = ttstr(str.c_str(), 50) + TJS_W("...");
				}
				AppendToNeuroConsole(L"\"", RGB(0, 128, 0));
				AppendToNeuroConsole(str.c_str(), RGB(0, 128, 0));
				AppendToNeuroConsole(L"\"\n", RGB(0, 128, 0));
				break;
			}
			case tvtInteger:
				swprintf_s(buf, L"%lld\n", value->AsInteger());
				AppendToNeuroConsole(buf, RGB(0, 0, 128));
				break;
			case tvtReal:
				swprintf_s(buf, L"%g\n", value->AsReal());
				AppendToNeuroConsole(buf, RGB(0, 0, 128));
				break;
			default:
				swprintf_s(buf, L"(type %d)\n", (int)vtype);
				AppendToNeuroConsole(buf, RGB(128, 128, 128));
				break;
		}
		return TJS_S_OK;
	}
};

// ============================================================================
// neuroGetCurrentContext() — сбор контекста игры для Neuro-sama
// Возвращает JSON строку с:
//   - languageType, languageTag
//   - backlogCount, recentEntries [{disp, text, voice, name}]
//   - currentSpeaker, currentText, currentVoice, currentName
//   - voicePrefix (3-буквенный префикс озвучки)
//   - voiceMap (prefix → charaName)
//   - currentScene, currentPage
// ============================================================================
static tjs_error TJS_INTF_METHOD
NeuroGetCurrentContext(tTJSVariant *result,
                       tjs_int numparams,
                       tTJSVariant **param,
                       iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("{\"error\":\"TJS not ready\"}");
		return TJS_S_OK;
	}

	// TJS код: собирает контекст, возвращает JSON
	// Используем IIFE для безопасного выполнения
	wchar_t const *tjsCode =
	    L"(function(){"
	    L"  try {"
	    L"    var r = {};"
	    L"    r.languageType = typeof kag !== 'undefined' ? kag.languageType : "
	    L"-1;"
	    L"    r.languageTag = typeof CurrentLanguageTag !== 'undefined' ? "
	    L"CurrentLanguageTag : '';"
	    L"    r.hooksActive = !!(global.__neuroHooksInstalled && "
	    L"global.__neuroHooksEnabled);"
	    L"    r.lastTag = global.__neuroLastTag || '';"
	    L"    r.lastScene = global.__neuroLastScene || '';"
	    L"    r.pendingSend = !!(global.__neuroPendingSend);"
	    L"    global.__neuroPendingSend = false;"
	    L"    if (typeof kag !== 'undefined' && kag.backlogWindow && "
	    L"kag.backlogWindow._historyTextStore) {"
	    L"      var store = kag.backlogWindow._historyTextStore;"
	    L"      var data = store.data;"
	    L"      var cnt = store.count;"
	    L"      r.backlogCount = cnt;"
	    L"      var entries = [];"
	    L"      var start = cnt > 5 ? cnt - 5 : 0;"
	    L"      for (var i = start; i < cnt; i++) {"
	    L"        var e = data[i];"
	    L"        "
	    L"entries.push({disp:e.disp,text:e.text,voice:e.voice,name:e.name});"
	    L"      }"
	    L"      r.recentEntries = entries;"
	    L"    }"
	    L"    var ml = (typeof kag !== 'undefined' && kag.messageLayer) ? "
	    L"kag.messageLayer : null;"
	    L"    if (ml && ml.message) {"
	    L"      r.currentText = ml.message;"
	    L"      r.currentSpeaker = ml.name || '';"
	    L"    } else if (typeof kag !== 'undefined' && kag.backlogWindow && "
	    L"kag.backlogWindow._historyTextStore) {"
	    L"      var store = kag.backlogWindow._historyTextStore;"
	    L"      if (store.count > 0) {"
	    L"        var last = store.data[store.count - 1];"
	    L"        r.currentText = last.text;"
	    L"        r.currentSpeaker = last.disp;"
	    L"      }"
	    L"    } else {"
	    L"      r.currentText = '';"
	    L"      r.currentSpeaker = '';"
	    L"    }"
	    L"    if (typeof kag !== 'undefined' && kag.backlogWindow && "
	    L"kag.backlogWindow._historyTextStore) {"
	    L"      var store = kag.backlogWindow._historyTextStore;"
	    L"      if (store.count > 0) {"
	    L"        var last = store.data[store.count - 1];"
	    L"        r.currentVoice = last.voice;"
	    L"        r.currentName = last.name;"
	    L"        if (last.voice && last.name && typeof characterVoiceInfo !== "
	    L"'undefined') {"
	    L"          var cinfo = characterVoiceInfo[last.name];"
	    L"          if (cinfo) r.voicePrefix = cinfo.file;"
	    L"        }"
	    L"      }"
	    L"    }"
	    L"    if (typeof characterVoiceInfo !== 'undefined') {"
	    L"      var vm = {};"
	    L"      var _cviKeys = Scripts.getObjectKeys(characterVoiceInfo);"
	    L"      for (var _cviIdx = 0; _cviIdx < _cviKeys.count; _cviIdx++) {"
	    L"        var n = _cviKeys[_cviIdx];"
	    L"        var f = characterVoiceInfo[n].file;"
	    L"        if (f) vm[f] = n;"
	    L"      }"
	    L"      r.voiceMap = vm;"
	    L"    }"
	    L"    r.currentScene = typeof kag !== 'undefined' ? kag.currentScene : "
	    L"'';"
	    L"    r.currentPage = typeof kag !== 'undefined' ? kag.currentPage : 0;"
	    L"    return System.toJSONString(r);"
	    L"  } catch(e) {"
	    L"    return '{\"error\":\"' + (e && e.message ? e.message : 'unknown') "
	    L"+ '\"}';"
	    L"  }"
	    L"})()";

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(tjsCode), &execResult);

	if (result) {
		*result = execResult;
	}

	return TJS_S_OK;
}

// neuroStartHooks() — устанавливает хуки
// onTag/onScenarioLoaded/onLabel/onVoiceStart Отслеживает релевантные теги
// (charaNew, playbgm, trans, и т.д.) и выставляет флаг для автоматической
// отправки контекста.
static tjs_error TJS_INTF_METHOD NeuroStartHooks(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("error: TJS not ready");
		return TJS_S_OK;
	}

	wchar_t const *tjsCode =
	    L"(function(){"
	    L"  try {"
	    L"    if (typeof kag === 'undefined') return 'kag not ready';"
	    L"    if (global.__neuroHooksInstalled) return 'already installed';"
	    L"    global.__neuroHooksEnabled = true;"
	    L"    global.__neuroLastTag = '';"
	    L"    global.__neuroLastScene = '';"
	    L"    global.__neuroPendingSend = false;"
	    L"    var relevant = "
	    L"{charaNew:1,charaDelete:1,playbgm:1,stopbgm:1,trans:1,playvoice:1,"
	    L"video:1,quake:1};"
	    L"    kag.addHook('onTag', function(tag, params) {"
	    L"      if (!global.__neuroHooksEnabled) return true;"
	    L"      global.__neuroLastTag = tag;"
	    L"      if (relevant[tag]) global.__neuroPendingSend = true;"
	    L"      return true;"
	    L"    });"
	    L"    kag.addHook('onScenarioLoaded', function(storage) {"
	    L"      if (!global.__neuroHooksEnabled) return;"
	    L"      global.__neuroLastScene = storage;"
	    L"      global.__neuroPendingSend = true;"
	    L"    });"
	    L"    kag.addHook('onLabel', function(label) {"
	    L"      if (!global.__neuroHooksEnabled) return;"
	    L"      global.__neuroPendingSend = true;"
	    L"    });"
	    L"    kag.addHook('onVoiceStart', function(name, file) {"
	    L"      if (!global.__neuroHooksEnabled) return;"
	    L"      global.__neuroPendingSend = true;"
	    L"    });"
	    L"    global.__neuroHooksInstalled = true;"
	    L"    return 'hooks installed';"
	    L"  } catch(e) { return 'error: ' + String(e); }"
	    L"})()";

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(tjsCode), &execResult);
	if (result)
		*result = execResult;
	return TJS_S_OK;
}

// neuroStopHooks() — отключает установленные хуки (через флаг)
static tjs_error TJS_INTF_METHOD NeuroStopHooks(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("error: TJS not ready");
		return TJS_S_OK;
	}

	wchar_t const *tjsCode =
	    L"(function(){"
	    L"  global.__neuroHooksEnabled = false;"
	    L"  return 'hooks stopped';"
	    L"})()";

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(tjsCode), &execResult);
	if (result)
		*result = execResult;
	return TJS_S_OK;
}

// neuroStartTagLogger([fileName]) — сбор KAG тегов в файл
// Устанавливает onTag хук, аккумулирует теги в буфере.
// fileName — опциональный путь к файлу (по умолчанию neuro_tag_log.txt)
// Храним имя файла лога в C++ (TJS-глобальные не выживают между
// TVPExecuteExpression)
static std::wstring g_TagLogFileName = L"neuro_tag_log.txt";

// neuroExecFile(filename) — читает .tjs файл из папки игры и выполняет
static tjs_error TJS_INTF_METHOD NeuroExecFile(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
	if (numparams < 1 || param[0]->Type() != tvtString) {
		if (result)
			*result = TJS_W("error: filename required");
		return TJS_S_OK;
	}

	ttstr filename = *param[0];

	// Читаем файл
	FILE *f = _wfopen(filename.c_str(), L"rb");
	if (!f) {
		if (result)
			*result = ttstr(L"error: cannot open '") + filename + L"'";
		return TJS_S_OK;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	std::vector<char> buf(size + 1);
	fread(buf.data(), 1, size, f);
	buf[size] = 0;
	fclose(f);

	// UTF-8 → UTF-16
	int wideLen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, NULL, 0);
	std::vector<wchar_t> wideBuf(wideLen);
	MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, wideBuf.data(), wideLen);

	// silent (2-й параметр) — подавляет логирование
	bool silent = (numparams >= 2 && param[1]->Type() == tvtInteger &&
	               (tjs_int)(*param[1]) != 0);

	if (!silent) {
		FILE *lf = fopen("plugin_log.txt", "a");
		if (lf) {
			auto toUtf8 = [](wchar_t const *w) -> std::string {
				int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
				if (len <= 0)
					return "";
				std::vector<char> b(len);
				WideCharToMultiByte(CP_UTF8, 0, w, -1, b.data(), len, NULL, NULL);
				return std::string(b.data());
			};
			std::string fn8 = toUtf8(filename.c_str());
			fprintf(lf, "\n=== EXEC FILE: %s ===\n", fn8.c_str());
			std::string narrowCode(buf.data());
			if (narrowCode.size() > 500)
				narrowCode.resize(500);
			fprintf(lf, "%s\n", narrowCode.c_str());
			fprintf(lf, "--- RESULT ---\n");
			fclose(lf);
		}
	}

	// Выполняем (может кинуть C++ исключение при syntax error — консоль покажет)
	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(wideBuf.data()), &execResult);

	if (!silent) {
		FILE *lf = fopen("plugin_log.txt", "a");
		if (lf) {
			auto toUtf8 = [](wchar_t const *w) -> std::string {
				int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
				if (len <= 0)
					return "";
				std::vector<char> b(len);
				WideCharToMultiByte(CP_UTF8, 0, w, -1, b.data(), len, NULL, NULL);
				return std::string(b.data());
			};
			ttstr resultStr(execResult);
			std::string res8 = toUtf8(resultStr.c_str());
			fprintf(lf, "%s\n", res8.c_str());
			fprintf(lf, "=== END ===\n");
			fclose(lf);
		}
	}

	if (result)
		*result = execResult;
	return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroStartTagLogger(tTJSVariant *result,
                                                     tjs_int numparams,
                                                     tTJSVariant **param,
                                                     iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("error: TJS not ready");
		return TJS_S_OK;
	}

	// Имя файла для лога (может быть передан параметром)
	g_TagLogFileName = L"neuro_tag_log.txt";
	if (numparams >= 1 && param[0]->Type() != tvtVoid) {
		ttstr fn(*param[0]);
		g_TagLogFileName = fn.c_str();
	}

	// Читаем и выполняем скрипт из файла
	wchar_t const *scriptPath = L"neuro_scripts\\tag_logger_start.tjs";
	FILE *f = _wfopen(scriptPath, L"rb");
	if (!f) {
		if (result)
			*result = ttstr(L"error: cannot open ") + scriptPath;
		return TJS_S_OK;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	std::vector<char> buf(size + 1);
	fread(buf.data(), 1, size, f);
	buf[size] = 0;
	fclose(f);

	// UTF-8 → UTF-16
	int wideLen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, NULL, 0);
	std::vector<wchar_t> wideBuf(wideLen);
	MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, wideBuf.data(), wideLen);

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(wideBuf.data()), &execResult);
	if (result)
		*result = execResult;
	return TJS_S_OK;
}

// neuroStopTagLogger() — останавливает сбор, сбрасывает буфер в файл
static tjs_error TJS_INTF_METHOD NeuroStopTagLogger(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("error: TJS not ready");
		return TJS_S_OK;
	}

	wchar_t const *scriptPath = L"neuro_scripts\\tag_logger_stop.tjs";
	FILE *f = _wfopen(scriptPath, L"rb");
	if (!f) {
		if (result)
			*result = ttstr(L"error: cannot open ") + scriptPath;
		return TJS_S_OK;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	std::vector<char> buf(size + 1);
	fread(buf.data(), 1, size, f);
	buf[size] = 0;
	fclose(f);

	int wideLen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, NULL, 0);
	std::vector<wchar_t> wideBuf(wideLen);
	MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, wideBuf.data(), wideLen);

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(wideBuf.data()), &execResult);

	// Пишем результат в файл
	FILE *lf = _wfopen(g_TagLogFileName.c_str(), L"a, ccs=UTF-8");
	if (lf) {
		fwprintf(lf, L"%s\n", execResult.GetString());
		fclose(lf);
	}

	if (result)
		*result = execResult;
	return TJS_S_OK;
}

// neuroFlushTagLog() — сбрасывает накопленные теги на диск, очищает буфер
static tjs_error TJS_INTF_METHOD NeuroFlushTagLog(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("error: TJS not ready");
		return TJS_S_OK;
	}

	tTJSVariant entriesJson;
	TVPExecuteExpression(
	    TJS_W("(function(){"
	          "  if (typeof global._neuroTagLoggerEntries == 'undefined') return "
	          "'[]';"
	          "  var e = System.toJSONString(global._neuroTagLoggerEntries);"
	          "  global._neuroTagLoggerEntries = [];"
	          "  return e;"
	          "})()"),
	    &entriesJson);

	ttstr entries = entriesJson;

	if (entries.c_str()[0]) {
		FILE *f = _wfopen(g_TagLogFileName.c_str(), L"a+, ccs=UTF-8");
		if (f) {
			fwprintf(f, L"%s\n", entries.c_str());
			fclose(f);
		}
	}

	if (result)
		*result = TJS_W("flushed");
	return TJS_S_OK;
}

// neuroBuildVoiceMap() — возвращает JSON: prefix → characterName
static tjs_error TJS_INTF_METHOD NeuroBuildVoiceMap(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
	if (!IsTJSReady()) {
		if (result)
			*result = TJS_W("{\"error\":\"TJS not ready\"}");
		return TJS_S_OK;
	}

	wchar_t const *tjsCode =
	    L"(function(){"
	    L"  try {"
	    L"    if (typeof characterVoiceInfo === 'undefined') return '{}';"
	    L"    var vm = {};"
	    L"    var keys = Scripts.getObjectKeys(characterVoiceInfo);"
	    L"    for (var i = 0; i < keys.count; i++) {"
	    L"      var n = keys[i];"
	    L"      var f = characterVoiceInfo[n].file;"
	    L"      if (f) vm[f] = n;"
	    L"    }"
	    L"    return System.toJSONString(vm);"
	    L"  } catch(e) {"
	    L"    return '{}';"
	    L"  }"
	    L"})()";

	tTJSVariant execResult;
	TVPExecuteExpression(ttstr(tjsCode), &execResult);

	if (result) {
		*result = execResult;
	}

	return TJS_S_OK;
}

// Дамп объекта - показывает все члены объекта
// Использование: neuroDump(obj) или neuroDump(obj, maxCount)
static tjs_error TJS_INTF_METHOD NeuroDump(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		AppendToNeuroConsole(L"Usage: neuroDump(object [, maxCount])\n",
		                     RGB(255, 0, 0));
		return TJS_S_OK;
	}

	int maxCount = 50;
	if (numparams >= 2 && param[1] && param[1]->Type() == tvtInteger) {
		maxCount = (int)param[1]->AsInteger();
		if (maxCount < 1)
			maxCount = 1;
		if (maxCount > 1000)
			maxCount = 1000;
	}

	tTJSVariantType type = param[0]->Type();

	if (type != tvtObject) {
		// Не объект - просто выводим значение
		wchar_t buf[256];
		swprintf_s(buf, L"Not an object. Type: %d\n", (int)type);
		AppendToNeuroConsole(buf, RGB(255, 128, 0));

		if (type == tvtString) {
			ttstr str = *param[0];
			AppendToNeuroConsole(L"Value: \"", RGB(0, 0, 255));
			AppendToNeuroConsole(str.c_str(), RGB(0, 128, 0));
			AppendToNeuroConsole(L"\"\n", RGB(0, 0, 255));
		} else if (type == tvtInteger) {
			swprintf_s(buf, L"Value: %lld\n", param[0]->AsInteger());
			AppendToNeuroConsole(buf, RGB(0, 0, 128));
		} else if (type == tvtReal) {
			swprintf_s(buf, L"Value: %g\n", param[0]->AsReal());
			AppendToNeuroConsole(buf, RGB(0, 0, 128));
		}
		return TJS_S_OK;
	}

	iTJSDispatch2 *obj = param[0]->AsObjectNoAddRef();
	if (!obj) {
		AppendToNeuroConsole(L"Object is null\n", RGB(255, 0, 0));
		return TJS_S_OK;
	}

	// Получаем имя класса
	tTJSVariant className;
	if (TJS_SUCCEEDED(obj->ClassInstanceInfo(TJS_CII_GET, 0, &className))) {
		ttstr classStr = className;
		AppendToNeuroConsole(L"=== Object: ", RGB(0, 100, 200));
		AppendToNeuroConsole(classStr.c_str(), RGB(128, 0, 128));
		AppendToNeuroConsole(L" ===\n", RGB(0, 100, 200));
	} else {
		AppendToNeuroConsole(L"=== Object (unknown class) ===\n", RGB(0, 100, 200));
	}

	// Создаём callback для перечисления
	tEnumMemberCallback *callback = new tEnumMemberCallback(maxCount);
	tTJSVariantClosure closure(callback, NULL);

	tjs_error err = obj->EnumMembers(0, &closure, obj);

	int memberCount = callback->m_count;
	callback->Release();

	if (TJS_FAILED(err)) {
		AppendToNeuroConsole(L"  (cannot enumerate members)\n", RGB(255, 0, 0));
	} else if (memberCount == 0) {
		AppendToNeuroConsole(L"  (no members)\n", RGB(128, 128, 128));
	}

	wchar_t buf[64];
	swprintf_s(buf, L"Total: %d members\n", memberCount);
	AppendToNeuroConsole(buf, RGB(100, 100, 100));

	return TJS_S_OK;
}

// Выполнение TJS кода (для консоли/отладки)
static tjs_error TJS_INTF_METHOD NeuroExec(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis) {
	if (numparams < 1 || !param || !param[0]) {
		return TJS_E_BADPARAMCOUNT;
	}

	if (!IsTJSReady()) {
		LogToFile(">>> NeuroExec: TJS not ready!\n");
		if (result) {
			*result = TJS_W("Error: TJS not initialized yet");
		}
		return TJS_S_OK;
	}

	if (param[0]->Type() == tvtString) {
		ttstr code = *param[0];
		LogToFile(">>> NeuroExec: executing TJS code\n");

		// TVPExecuteExpression не бросает исключения в плагине,
		// ошибки обрабатываются внутри движка
		tTJSVariant execResult;
		TVPExecuteExpression(code, &execResult);

		if (result) {
			*result = execResult;
		}
	}

	return TJS_S_OK;
}

// ============================================================================
// Регистрация функций через ncbind
// ============================================================================
// Макросы NCB_REGISTER_FUNCTION регистрируют функции в TJS при вызове
// ncbAutoRegister::AllRegist(). Расположены в конце файла перед DllMain.

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

	// ncbAutoRegister::AllRegist() будет вызван в DeferredTJSInitThread после
	// готовности TJS
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
	ncbAutoRegister::AllUnregist();

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
// NCBIND — регистрация TJS функций
// ============================================================================
NCB_REGISTER_FUNCTION(neuroTest, NeuroTestFunction);
NCB_REGISTER_FUNCTION(neuroGetVersion, NeuroGetVersion);
NCB_REGISTER_FUNCTION(neuroEcho, NeuroEcho);
NCB_REGISTER_FUNCTION(neuroSendData, NeuroSendData);
NCB_REGISTER_FUNCTION(neuroIsTJSReady, NeuroIsTJSReady);
NCB_REGISTER_FUNCTION(neuroIsKAGReady, NeuroIsKAGReady);
NCB_REGISTER_FUNCTION(neuroShowConsole, NeuroShowConsole);
NCB_REGISTER_FUNCTION(neuroExec, NeuroExec);
NCB_REGISTER_FUNCTION(neuroConsoleLog, NeuroConsoleLog);
NCB_REGISTER_FUNCTION(neuroDump, NeuroDump);
NCB_REGISTER_FUNCTION(neuroGetCurrentContext, NeuroGetCurrentContext);
NCB_REGISTER_FUNCTION(neuroBuildVoiceMap, NeuroBuildVoiceMap);
NCB_REGISTER_FUNCTION(neuroStartHooks, NeuroStartHooks);
NCB_REGISTER_FUNCTION(neuroStopHooks, NeuroStopHooks);
NCB_REGISTER_FUNCTION(neuroExecFile, NeuroExecFile);
NCB_REGISTER_FUNCTION(neuroStartTagLogger, NeuroStartTagLogger);
NCB_REGISTER_FUNCTION(neuroStopTagLogger, NeuroStopTagLogger);
NCB_REGISTER_FUNCTION(neuroFlushTagLog, NeuroFlushTagLog);

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
