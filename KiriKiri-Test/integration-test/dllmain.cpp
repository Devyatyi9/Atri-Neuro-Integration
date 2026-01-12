#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>  // SetWindowSubclass
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include "../include/neurosdk.h"
#include "NeuroConsole.h"

#pragma comment(lib, "comctl32.lib")

// ============================================================================
// KiriKiri SDK - tp_stub (для консоли и TVPExecuteExpression)
// ============================================================================
#pragma warning(push)
#pragma warning(disable: 26495)  // Uninitialized member variable (tp_stub)
#include "../krkr-sdk/tp_stub.h"
#pragma warning(pop)

// ============================================================================
// Глобальные переменные для отложенной инициализации
// ============================================================================
static std::atomic<bool> g_TJSReady(false);
static std::atomic<bool> g_InitThreadRunning(false);
static std::atomic<bool> g_HotkeyThreadRunning(false);
static iTJSDispatch2* g_ScriptDispatch = nullptr;
static std::thread g_HotkeyThread;  // Для корректного join()
static DWORD g_MainThreadId = 0;    // ID главного потока игры

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
// Буферизованное логирование
// ============================================================================
static FILE* g_LogFile = nullptr;
static std::mutex g_LogMutex;

void LogToFile(const char* message) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    if (!g_LogFile) {
        g_LogFile = fopen("plugin_log.txt", "a");
        if (!g_LogFile) return;
    }
    fprintf(g_LogFile, "%s", message);
    fflush(g_LogFile);  // Гарантирует запись при краше
}

void CloseLogFile() {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    if (g_LogFile) {
        fclose(g_LogFile);
        g_LogFile = nullptr;
    }
}

// ============================================================================
// Self-linking: поиск iTVPFunctionExporter в памяти
// ============================================================================
// RVA адреса для ATRI -My Dear Moments- (обновить для другой игры/версии)
const DWORD RVA_EXPORTER = 0x003B4538;
const DWORD RVA_VTABLE = 0x00374A50;

static DWORD g_BaseAddress = 0;

iTVPFunctionExporter* WaitForExporterInit(int maxWaitMs = 10000) {
    int elapsed = 0;
    const int checkInterval = 100;
    bool mismatchReported = false;

    char logBuf[256];
    sprintf_s(logBuf, "Waiting for exporter (RVA=0x%08X, vtable RVA=0x%08X)...\n",
        RVA_EXPORTER, RVA_VTABLE);
    LogToFile(logBuf);

    while (elapsed < maxWaitMs) {
        HMODULE hMain = GetModuleHandle(nullptr);
        if (!hMain) {
            Sleep(checkInterval);
            elapsed += checkInterval;
            continue;
        }

        BYTE* baseAddress = (BYTE*)hMain;
        g_BaseAddress = (DWORD)baseAddress;
        iTVPFunctionExporter* exporter = (iTVPFunctionExporter*)(baseAddress + RVA_EXPORTER);

        if (!IsBadReadPtr(exporter, sizeof(void*))) {
            void** vtable = *(void***)exporter;
            void* expectedVTable = (void*)(baseAddress + RVA_VTABLE);

            if (vtable == expectedVTable) {
                sprintf_s(logBuf, "Exporter found after %d ms at 0x%08X\n", elapsed, (DWORD)exporter);
                LogToFile(logBuf);
                return exporter;
            }

            // VTable не совпадает — сразу показываем ошибку (один раз)
            if (!mismatchReported && vtable != nullptr) {
                mismatchReported = true;

                DWORD actualRVA = (DWORD)((BYTE*)vtable - baseAddress);

                sprintf_s(logBuf, "ERROR: VTable mismatch! Expected RVA=0x%08X, Actual RVA=0x%08X\n",
                    RVA_VTABLE, actualRVA);
                LogToFile(logBuf);

                wchar_t msg[512];
                swprintf_s(msg,
                    L"VTable mismatch detected!\n\n"
                    L"Expected: 0x%08X (RVA: 0x%08X)\n"
                    L"Actual:   0x%08X\n\n"
                    L"Update RVA_VTABLE in plugin dllmain.cpp\n"
                    L"Actual RVA = 0x%08X",
                    (DWORD)expectedVTable, RVA_VTABLE,
                    (DWORD)vtable, actualRVA);

                MessageBoxW(nullptr, msg, L"Neuro Plugin - RVA Error", MB_OK | MB_ICONWARNING);
                return nullptr;
            }
        }

        Sleep(checkInterval);
        elapsed += checkInterval;

        if (elapsed % 1000 == 0) {
            sprintf_s(logBuf, "Still waiting... %d/%d ms\n", elapsed, maxWaitMs);
            LogToFile(logBuf);
        }
    }

    LogToFile("ERROR: Exporter init timeout!\n");
    return nullptr;
}

// ============================================================================
// Neuro SDK - Callback и вспомогательные функции
// ============================================================================
#pragma region Neuro
// Callback для логов от Neuro SDK
void NeuroLogCallback(neurosdk_severity_e severity, char* message, void* user_data) {
    const char* severityStr = "???";
    switch (severity) {
        case NeuroSDK_Severity_Debug: severityStr = "DEBUG"; break;
        case NeuroSDK_Severity_Info:  severityStr = "INFO"; break;
        case NeuroSDK_Severity_Warn:  severityStr = "WARN"; break;
        case NeuroSDK_Severity_Error: severityStr = "ERROR"; break;
    }

    char buf[1024];
    sprintf_s(buf, "[NeuroSDK %s] %s\n", severityStr, message ? message : "(null)");
    LogToFile(buf);
}
#pragma endregion
// Forward declarations для TJS execution
void ExecuteTJSAsync(const wchar_t* code);
bool ExecuteTJSSync(const wchar_t* code, tTJSVariant* result, DWORD timeoutMs);

#pragma region Neuro2
// Обработка входящего Action от Neuro
void HandleNeuroAction(const char* id, const char* name, const char* data) {
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
        id ? id : "",
        name ? name : "",
        data ? data : "");

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

        neurosdk_message_t* messages = nullptr;
        int count = 0;

        neurosdk_error_e err = neurosdk_context_poll(&g_NeuroContext, &messages, &count);

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
            neurosdk_message_t* msg = &messages[i];

            if (msg->kind == NeuroSDK_MessageKind_Action) {
                HandleNeuroAction(
                    msg->value.action.id,
                    msg->value.action.name,
                    msg->value.action.data);
            }

            neurosdk_message_destroy(msg);
        }

        Sleep(50);  // poll interval
    }

    LogToFile("[Neuro] Poll thread stopped\n");
}

// Подключение к Neuro
bool NeuroConnect(const char* url, const char* gameName) {
    if (g_NeuroConnected.load()) {
        LogToFile("[Neuro] Already connected\n");
        return true;
    }

    if (url) g_NeuroURL = url;
    if (gameName) g_NeuroGameName = gameName;

    char buf[512];
    sprintf_s(buf, "[Neuro] Connecting to %s as '%s'...\n",
        g_NeuroURL.c_str(), g_NeuroGameName.c_str());
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
        sprintf_s(buf, "[Neuro] Connection failed: %s\n", neurosdk_error_string(err));
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
bool NeuroSendContext(const char* message, bool silent) {
    if (!g_NeuroConnected.load() || !g_NeuroContext) {
        LogToFile("[Neuro] Cannot send context: not connected\n");
        return false;
    }

    neurosdk_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.kind = NeuroSDK_MessageKind_Context;
    msg.value.context.message = const_cast<char*>(message);
    msg.value.context.silent = silent;

    neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
    if (err != NeuroSDK_None) {
        char buf[256];
        sprintf_s(buf, "[Neuro] Send context failed: %s\n", neurosdk_error_string(err));
        LogToFile(buf);
        return false;
    }

    char buf[512];
    sprintf_s(buf, "[Neuro] Context sent: %s\n", message);
    LogToFile(buf);
    return true;
}

// Регистрация action в Neuro
bool NeuroRegisterAction(const char* name, const char* description, const char* jsonSchema) {
    if (!g_NeuroConnected.load() || !g_NeuroContext) {
        LogToFile("[Neuro] Cannot register action: not connected\n");
        return false;
    }

    neurosdk_action_t action;
    action.name = const_cast<char*>(name);
    action.description = const_cast<char*>(description);
    action.json_schema = const_cast<char*>(jsonSchema ? jsonSchema : "{}");

    neurosdk_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.kind = NeuroSDK_MessageKind_ActionsRegister;
    msg.value.actions_register.actions = &action;
    msg.value.actions_register.actions_len = 1;

    neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
    if (err != NeuroSDK_None) {
        char buf[256];
        sprintf_s(buf, "[Neuro] Register action failed: %s\n", neurosdk_error_string(err));
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
bool NeuroSendActionResult(const char* id, bool success, const char* message) {
    if (!g_NeuroConnected.load() || !g_NeuroContext) {
        LogToFile("[Neuro] Cannot send result: not connected\n");
        return false;
    }

    neurosdk_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.kind = NeuroSDK_MessageKind_ActionResult;
    msg.value.action_result.id = const_cast<char*>(id);
    msg.value.action_result.success = success;
    msg.value.action_result.message = const_cast<char*>(message ? message : "");

    neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
    if (err != NeuroSDK_None) {
        char buf[256];
        sprintf_s(buf, "[Neuro] Send result failed: %s\n", neurosdk_error_string(err));
        LogToFile(buf);
        return false;
    }

    char buf[256];
    sprintf_s(buf, "[Neuro] Result sent for %s: %s\n", id, success ? "success" : "failure");
    LogToFile(buf);
    return true;
}

// Force action (попросить Neuro выполнить действие)
bool NeuroForceAction(const char* state, const char* query, const char** actionNames, int actionCount, bool ephemeral) {
    if (!g_NeuroConnected.load() || !g_NeuroContext) {
        LogToFile("[Neuro] Cannot force action: not connected\n");
        return false;
    }

    neurosdk_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.kind = NeuroSDK_MessageKind_ActionsForce;
    msg.value.actions_force.state = const_cast<char*>(state ? state : "");
    msg.value.actions_force.query = const_cast<char*>(query ? query : "");
    msg.value.actions_force.ephemeral_context = ephemeral;
    msg.value.actions_force.action_names = const_cast<char**>(actionNames);
    msg.value.actions_force.action_names_len = actionCount;
    msg.value.actions_force.priority = NeuroSDK_Priority_Medium;

    neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
    if (err != NeuroSDK_None) {
        char buf[256];
        sprintf_s(buf, "[Neuro] Force action failed: %s\n", neurosdk_error_string(err));
        LogToFile(buf);
        return false;
    }

    LogToFile("[Neuro] Force action sent\n");
    return true;
}
#pragma endregion
// ============================================================================
// Thread-safe TJS execution через Window Subclass
// ============================================================================
// ВАЖНО: TJS2 НЕ потокобезопасен! Все вызовы TVPExecuteExpression и т.п.
// должны происходить из главного потока.
//
// Решение: Subclass главного окна игры и обрабатывать наши сообщения там.
// Это гарантирует что сообщения обрабатываются в message loop игры.
// ============================================================================
static HWND g_GameWindow = nullptr;
static WNDPROC g_OriginalWndProc = nullptr;
static const UINT WM_TJS_EXECUTE = WM_USER + 100;
static const UINT WM_TJS_EXECUTE_SYNC = WM_USER + 101;

// Структура для синхронного вызова с результатом (выделяется в куче!)
struct TJSSyncCallData {
    const wchar_t* code;
    tTJSVariant* result;
    HANDLE completedEvent;
    std::atomic<bool> processed;  // Флаг обработки для безопасного удаления

    TJSSyncCallData() : code(nullptr), result(nullptr), completedEvent(nullptr), processed(false) {}
};

// Проверка главного потока
bool IsMainThread() {
    return GetCurrentThreadId() == g_MainThreadId;
}

// Счётчик для отладки - сколько раз вызван SubclassedWndProc
static std::atomic<int> g_SubclassCallCount(0);

// Subclassed Window Procedure
LRESULT CALLBACK SubclassedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Считаем вызовы (для диагностики)
    g_SubclassCallCount++;

    switch (msg) {
    case WM_TJS_EXECUTE: {
        // Асинхронный вызов - код передаётся как копия, нужно освободить
        LogToFile("  [Subclass] WM_TJS_EXECUTE received!\n");
        wchar_t* code = (wchar_t*)wParam;
        if (code && g_TJSReady.load()) {
            LogToFile("  [Subclass] Executing TJS async\n");
            tTJSVariant dummy;
            TVPExecuteExpression(ttstr(code), &dummy);
        }
        delete[] code;
        return 0;
    }

    case WM_TJS_EXECUTE_SYNC: {
        // Синхронный вызов - возвращает результат
        LogToFile("  [Subclass] WM_TJS_EXECUTE_SYNC received!\n");
        TJSSyncCallData* data = (TJSSyncCallData*)wParam;
        if (data) {
            if (data->code && g_TJSReady.load()) {
                LogToFile("  [Subclass] Executing TJS sync\n");
                TVPExecuteExpression(ttstr(data->code), data->result);
            }
            data->processed.store(true);  // Помечаем как обработанное
            if (data->completedEvent) {
                SetEvent(data->completedEvent);
            }
        }
        return 0;
    }
    }

    // Передаём все остальные сообщения оригинальной процедуре
    return CallWindowProc(g_OriginalWndProc, hwnd, msg, wParam, lParam);
}

// Структура для передачи данных в EnumWindows
struct EnumWindowsData {
    HWND foundWindow;
    HWND kirikiriWindow;  // Приоритетное окно KiriKiri
};

// Callback для EnumWindows - ищем главное окно нашего процесса
BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid == GetCurrentProcessId()) {
        // Проверяем что это видимое окно верхнего уровня
        if (IsWindowVisible(hwnd) && GetParent(hwnd) == NULL) {
            // Получаем класс окна
            wchar_t className[256] = {0};
            GetClassNameW(hwnd, className, 256);

            EnumWindowsData* data = (EnumWindowsData*)lParam;

            // Пропускаем диалоговые окна (#32770)
            if (wcscmp(className, L"#32770") == 0) {
                return TRUE;  // Продолжаем искать
            }

            // Приоритет для окна KiriKiri (TTVPWindowForm)
            if (wcsstr(className, L"TTVPWindowForm") != nullptr ||
                wcsstr(className, L"TVPMainWindow") != nullptr) {
                data->kirikiriWindow = hwnd;
                return FALSE;  // Нашли окно KiriKiri, прекращаем
            }

            // Сохраняем первое подходящее окно как fallback
            if (!data->foundWindow) {
                data->foundWindow = hwnd;
            }
        }
    }
    return TRUE;
}

// Subclass главного окна игры
bool SubclassGameWindow() {
    // Ждём появления окна игры (до 5 секунд)
    LogToFile("Waiting for game window...\n");

    for (int attempt = 0; attempt < 50; attempt++) {
        EnumWindowsData enumData = {nullptr, nullptr};
        EnumWindows(EnumWindowsCallback, (LPARAM)&enumData);

        g_GameWindow = enumData.kirikiriWindow ? enumData.kirikiriWindow : enumData.foundWindow;

        if (g_GameWindow) {
            char buf[128];
            sprintf_s(buf, "  Found window on attempt %d\n", attempt + 1);
            LogToFile(buf);
            break;
        }

        Sleep(100);  // Ждём 100мс между попытками
    }

    if (!g_GameWindow) {
        LogToFile("ERROR: Could not find game window after 5 seconds\n");
        return false;
    }

    char buf[512];
    sprintf_s(buf, "Found game window: HWND=%p\n", g_GameWindow);
    LogToFile(buf);

    // Получаем информацию об окне для диагностики
    wchar_t className[256] = {0};
    wchar_t windowTitle[256] = {0};
    GetClassNameW(g_GameWindow, className, 256);
    GetWindowTextW(g_GameWindow, windowTitle, 256);

    char diagBuf[1024];
    sprintf_s(diagBuf, "  Window class: %ls\n  Window title: %ls\n",
        className, windowTitle);
    LogToFile(diagBuf);

    // Получаем и сохраняем thread ID окна (главный поток игры)
    g_MainThreadId = GetWindowThreadProcessId(g_GameWindow, NULL);
    DWORD currentThreadId = GetCurrentThreadId();
    sprintf_s(diagBuf, "  Window thread (main): %lu, Current thread: %lu\n",
        g_MainThreadId, currentThreadId);
    LogToFile(diagBuf);

    // Subclass окно через SetWindowLongPtr
    g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)SubclassedWndProc);

    if (!g_OriginalWndProc) {
        DWORD err = GetLastError();
        sprintf_s(diagBuf, "ERROR: SetWindowLongPtr failed, error=%lu\n", err);
        LogToFile(diagBuf);
        g_GameWindow = nullptr;
        return false;
    }

    LogToFile("Game window subclassed successfully\n");
    return true;
}

// Восстановление оригинальной процедуры окна
void UnsubclassGameWindow() {
    if (g_GameWindow && g_OriginalWndProc) {
        SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
        LogToFile("Game window unsubclassed\n");
    }
    g_GameWindow = nullptr;
    g_OriginalWndProc = nullptr;
}

// ============================================================================
// Thread-safe TJS execution functions
// ============================================================================

// Асинхронный вызов TJS (fire-and-forget)
// Можно вызывать из любого потока!
void ExecuteTJSAsync(const wchar_t* code) {
    if (!g_GameWindow || !code) return;

    // Копируем строку (будет удалена в обработчике)
    size_t len = wcslen(code) + 1;
    wchar_t* codeCopy = new wchar_t[len];
    wcscpy_s(codeCopy, len, code);

    PostMessage(g_GameWindow, WM_TJS_EXECUTE, (WPARAM)codeCopy, 0);
}

// Синхронный вызов TJS с ожиданием результата
// Можно вызывать из любого потока!
bool ExecuteTJSSync(const wchar_t* code, tTJSVariant* result, DWORD timeoutMs) {
    if (!g_GameWindow || !IsWindow(g_GameWindow) || !code) return false;

    // Если вызов из главного потока - выполняем напрямую (избегаем deadlock)
    if (IsMainThread()) {
        if (g_TJSReady.load()) {
            TVPExecuteExpression(ttstr(code), result);
            return true;
        }
        return false;
    }

    // Выделяем в куче для безопасности при timeout
    TJSSyncCallData* data = new TJSSyncCallData();
    data->code = code;
    data->result = result;
    data->completedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!data->completedEvent) {
        delete data;
        return false;
    }

    char diagBuf[256];
    sprintf_s(diagBuf, "  [ExecuteTJSSync] Posting to HWND=%p, subclass calls so far: %d\n",
        g_GameWindow, g_SubclassCallCount.load());
    LogToFile(diagBuf);

    BOOL postResult = PostMessage(g_GameWindow, WM_TJS_EXECUTE_SYNC, (WPARAM)data, 0);
    if (!postResult) {
        DWORD err = GetLastError();
        sprintf_s(diagBuf, "  [ExecuteTJSSync] PostMessage FAILED! Error=%lu\n", err);
        LogToFile(diagBuf);
        CloseHandle(data->completedEvent);
        delete data;
        return false;
    }

    DWORD waitResult = WaitForSingleObject(data->completedEvent, timeoutMs);
    CloseHandle(data->completedEvent);
    data->completedEvent = nullptr;

    if (waitResult == WAIT_OBJECT_0) {
        // Успешно обработано - безопасно удаляем
        delete data;
        return true;
    } else {
        // Timeout! Сообщение может быть ещё в очереди.
        // Проверяем, было ли обработано
        sprintf_s(diagBuf, "  [ExecuteTJSSync] TIMEOUT! Subclass calls after: %d\n",
            g_SubclassCallCount.load());
        LogToFile(diagBuf);

        if (data->processed.load()) {
            // Обработано после timeout - безопасно удаляем
            delete data;
        } else {
            // НЕ удаляем! Оставляем утечку - лучше чем crash
            // Сообщение ещё в очереди и будет обрабатывать этот указатель
            LogToFile("  [ExecuteTJSSync] WARNING: Memory leak to prevent use-after-free\n");
        }
        return false;
    }
}

// ============================================================================
// Отложенная инициализация TJS
// ============================================================================

void TryInitTJS() {
    if (g_TJSReady.load()) return;

    __try {
        iTJSDispatch2* dispatch = TVPGetScriptDispatch();
        if (dispatch) {
            g_ScriptDispatch = dispatch;
            g_TJSReady.store(true);
            LogToFile("TJS initialized successfully!\n");
            OutputDebugStringW(L"[NEURO PLUGIN] TJS initialized!\n");
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LogToFile("TryInitTJS: EXCEPTION caught!\n");
    }
}

// Forward declarations
bool RegisterAllFunctions();
void StartHotkeyThread();

void DeferredTJSInitThread() {
    LogToFile("Deferred TJS init thread started...\n");

    const int maxAttempts = 100;  // 10 секунд максимум
    const int intervalMs = 100;

    for (int i = 0; i < maxAttempts && !g_TJSReady.load(); i++) {
        Sleep(intervalMs);
        TryInitTJS();

        // Логируем каждую секунду
        if ((i + 1) % 10 == 0) {
            char buf[64];
            sprintf_s(buf, "  Waiting for TJS... attempt %d/%d\n", i + 1, maxAttempts);
            LogToFile(buf);
        }
    }

    if (g_TJSReady.load()) {
        LogToFile("Deferred TJS init: SUCCESS\n");

        // Subclass окна игры для thread-safe TJS вызовов
        if (!SubclassGameWindow()) {
            LogToFile("WARNING: Window subclass failed, TJS calls may be unsafe\n");
        }

        // Регистрируем функции в TJS
        if (RegisterAllFunctions()) {
            OutputDebugStringW(L"[NEURO PLUGIN] Functions registered successfully!\n");

            // Тестируем вызов зарегистрированных функций из C++
            LogToFile("Testing registered functions from C++...\n");

            tTJSVariant result;
            char buf[256];

            // Тест 1: neuroGetVersion()
            TVPExecuteExpression(TJS_W("neuroGetVersion()"), &result);
            sprintf_s(buf, "  neuroGetVersion() returned: %d\n", (int)(tTVInteger)result);
            LogToFile(buf);

            // Тест 2: neuroIsTJSReady()
            TVPExecuteExpression(TJS_W("neuroIsTJSReady()"), &result);
            sprintf_s(buf, "  neuroIsTJSReady() returned: %d\n", (int)(tTVInteger)result);
            LogToFile(buf);

            LogToFile("Basic tests completed!\n");
            LogToFile("Hotkey: F12=console\n");

            // Запускаем обработчик хоткеев
            StartHotkeyThread();
        } else {
            LogToFile("ERROR: Failed to register functions\n");
        }
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
            //LogToFile("F12 pressed - toggling Neuro Console\n");
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

// Проверка готовности TJS (можно вызывать из TJS функций)
bool IsTJSReady() {
    if (g_TJSReady.load()) return true;
    TryInitTJS();  // Попробуем инициализировать при первом вызове
    return g_TJSReady.load();
}

// ============================================================================
// TJS функции (простые обёртки для регистрации через exporter)
// ============================================================================

static tjs_error TJS_INTF_METHOD NeuroTestFunction(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    OutputDebugStringW(L"[NEURO PLUGIN] NeuroTestFunction called from TJS!\n");
    LogToFile(">>> NeuroTestFunction CALLED FROM TJS SCRIPT!\n");

    MessageBoxW(NULL,
        L"SUCCESS!\n\n"
        L"NeuroTestFunction was successfully called from TJS!\n\n"
        L"Your Neuro-integration plugin is fully working!",
        L"Neuro Plugin - Function Called!",
        MB_OK | MB_ICONINFORMATION);

    if (result) {
        *result = TJS_W("Hello from Neuro-integration plugin!");
    }

    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroGetVersion(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    OutputDebugStringW(L"[NEURO PLUGIN] NeuroGetVersion called!\n");
    LogToFile(">>> NeuroGetVersion called\n");

    if (result) {
        *result = (tTVInteger)100; // Version 1.0.0
    }

    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD NeuroEcho(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    OutputDebugStringW(L"[NEURO PLUGIN] NeuroEcho called!\n");
    LogToFile(">>> NeuroEcho called\n");

    if (numparams > 0 && param && param[0]) {
        tTJSVariant* firstParam = param[0];

        if (firstParam->Type() == tvtString) {
            ttstr str = *firstParam;
            MessageBoxW(NULL, str.c_str(), L"Neuro Echo", MB_OK | MB_ICONINFORMATION);

            if (result) {
                *result = *firstParam;
            }
        }
        else if (firstParam->Type() == tvtInteger) {
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

static tjs_error TJS_INTF_METHOD NeuroSendData(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    LogToFile(">>> NeuroSendData called with ");

    char logMsg[128];
    sprintf_s(logMsg, sizeof(logMsg), "%d parameters\n", numparams);
    LogToFile(logMsg);

    // TODO: Здесь будет интеграция с libneurosdk

    if (result) {
        *result = (tTVInteger)1; // Успешно
    }

    return TJS_S_OK;
}

// Показать встроенную консоль Debug
static tjs_error TJS_INTF_METHOD NeuroShowConsole(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    LogToFile(">>> NeuroShowConsole called\n");

    tTJSVariant checkResult;
    TVPExecuteExpression(TJS_W("typeof Debug != 'undefined' && typeof Debug.console != 'undefined'"), &checkResult);

    if ((tTVInteger)checkResult != 0) {
        tTJSVariant dummy;
        TVPExecuteExpression(TJS_W("Debug.console.visible = true"), &dummy);
        LogToFile("  Debug.console.visible = true\n");
        if (result) *result = (tTVInteger)1;
    } else {
        LogToFile("  Debug.console not available\n");
        if (result) *result = (tTVInteger)0;
    }

    return TJS_S_OK;
}

// Проверка готовности KAG
static tjs_error TJS_INTF_METHOD NeuroIsKAGReady(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    tTJSVariant checkResult;
    TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined'"), &checkResult);
    bool ready = (tTVInteger)checkResult != 0;

    LogToFile(ready ? ">>> NeuroIsKAGReady: YES\n" : ">>> NeuroIsKAGReady: NO\n");

    if (result) *result = (tTVInteger)(ready ? 1 : 0);
    return TJS_S_OK;
}

// Проверка готовности TJS (для отладки)
static tjs_error TJS_INTF_METHOD NeuroIsTJSReady(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    bool ready = IsTJSReady();
    LogToFile(ready ? ">>> NeuroIsTJSReady: YES\n" : ">>> NeuroIsTJSReady: NO\n");

    if (result) {
        *result = (tTVInteger)(ready ? 1 : 0);
    }

    return TJS_S_OK;
}

// Добавить текст в NeuroConsole (для перехвата Debug.message)
static tjs_error TJS_INTF_METHOD NeuroConsoleLog(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
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
class tEnumMemberCallback : public tTJSDispatch
{
public:
    int m_count;
    int m_maxCount;

    tEnumMemberCallback(int maxCount = 50) : m_count(0), m_maxCount(maxCount) {}

    tjs_error TJS_INTF_METHOD FuncCall(
        tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
        tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
        iTJSDispatch2* objthis) override
    {
        // EnumMembers вызывает callback(name, flags, value)
        if (numparams < 3) return TJS_S_OK;

        ttstr name = *param[0];
        // param[1] = flags
        tTJSVariant* value = param[2];

        if (name.IsEmpty()) return TJS_S_OK;

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
                iTJSDispatch2* memberObj = value->AsObjectNoAddRef();
                if (memberObj) {
                    tTJSVariant memberClass;
                    if (TJS_SUCCEEDED(memberObj->ClassInstanceInfo(TJS_CII_GET, 0, &memberClass))) {
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

// Дамп объекта - показывает все члены объекта
// Использование: neuroDump(obj) или neuroDump(obj, maxCount)
static tjs_error TJS_INTF_METHOD NeuroDump(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
    if (numparams < 1 || !param || !param[0]) {
        AppendToNeuroConsole(L"Usage: neuroDump(object [, maxCount])\n", RGB(255, 0, 0));
        return TJS_S_OK;
    }

    int maxCount = 50;
    if (numparams >= 2 && param[1] && param[1]->Type() == tvtInteger) {
        maxCount = (int)param[1]->AsInteger();
        if (maxCount < 1) maxCount = 1;
        if (maxCount > 1000) maxCount = 1000;
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

    iTJSDispatch2* obj = param[0]->AsObjectNoAddRef();
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
    tEnumMemberCallback* callback = new tEnumMemberCallback(maxCount);
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
static tjs_error TJS_INTF_METHOD NeuroExec(
    tTJSVariant* result,
    tjs_int numparams,
    tTJSVariant** param,
    iTJSDispatch2* objthis)
{
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
// Регистрация функций в TJS глобальном объекте
// ============================================================================

// Класс-обёртка для TJS функции
template<tjs_error (TJS_INTF_METHOD *Func)(tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2*)>
class tTJSFunctionWrapper : public tTJSDispatch
{
public:
    tjs_error TJS_INTF_METHOD FuncCall(
        tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
        tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
        iTJSDispatch2* objthis) override
    {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        return Func(result, numparams, param, objthis);
    }
};

// Глобальные указатели на функции-обёртки
static tTJSFunctionWrapper<NeuroTestFunction>* g_NeuroTestFunc = nullptr;
static tTJSFunctionWrapper<NeuroGetVersion>* g_NeuroGetVersionFunc = nullptr;
static tTJSFunctionWrapper<NeuroEcho>* g_NeuroEchoFunc = nullptr;
static tTJSFunctionWrapper<NeuroSendData>* g_NeuroSendDataFunc = nullptr;
static tTJSFunctionWrapper<NeuroIsTJSReady>* g_NeuroIsTJSReadyFunc = nullptr;
static tTJSFunctionWrapper<NeuroIsKAGReady>* g_NeuroIsKAGReadyFunc = nullptr;
static tTJSFunctionWrapper<NeuroShowConsole>* g_NeuroShowConsoleFunc = nullptr;
static tTJSFunctionWrapper<NeuroExec>* g_NeuroExecFunc = nullptr;
static tTJSFunctionWrapper<NeuroConsoleLog>* g_NeuroConsoleLogFunc = nullptr;
static tTJSFunctionWrapper<NeuroDump>* g_NeuroDumpFunc = nullptr;

void RegisterTJSFunction(iTJSDispatch2* global, const tjs_char* name, tTJSDispatch* func)
{
    // Используем конструктор с явным указанием объекта
    tTJSVariant val(func, func);
    func->Release();  // val теперь держит ссылку
    global->PropSet(TJS_MEMBERENSURE, name, NULL, &val, global);
}

bool RegisterAllFunctions()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global) {
        LogToFile("RegisterAllFunctions: global is NULL\n");
        return false;
    }

    LogToFile("Registering TJS functions via PropSet...\n");

    // Создаём и регистрируем функции
    g_NeuroTestFunc = new tTJSFunctionWrapper<NeuroTestFunction>();
    RegisterTJSFunction(global, TJS_W("neuroTest"), g_NeuroTestFunc);

    g_NeuroGetVersionFunc = new tTJSFunctionWrapper<NeuroGetVersion>();
    RegisterTJSFunction(global, TJS_W("neuroGetVersion"), g_NeuroGetVersionFunc);

    g_NeuroEchoFunc = new tTJSFunctionWrapper<NeuroEcho>();
    RegisterTJSFunction(global, TJS_W("neuroEcho"), g_NeuroEchoFunc);

    g_NeuroSendDataFunc = new tTJSFunctionWrapper<NeuroSendData>();
    RegisterTJSFunction(global, TJS_W("neuroSendData"), g_NeuroSendDataFunc);

    g_NeuroIsTJSReadyFunc = new tTJSFunctionWrapper<NeuroIsTJSReady>();
    RegisterTJSFunction(global, TJS_W("neuroIsTJSReady"), g_NeuroIsTJSReadyFunc);

    g_NeuroIsKAGReadyFunc = new tTJSFunctionWrapper<NeuroIsKAGReady>();
    RegisterTJSFunction(global, TJS_W("neuroIsKAGReady"), g_NeuroIsKAGReadyFunc);

    g_NeuroShowConsoleFunc = new tTJSFunctionWrapper<NeuroShowConsole>();
    RegisterTJSFunction(global, TJS_W("neuroShowConsole"), g_NeuroShowConsoleFunc);

    g_NeuroExecFunc = new tTJSFunctionWrapper<NeuroExec>();
    RegisterTJSFunction(global, TJS_W("neuroExec"), g_NeuroExecFunc);

    g_NeuroConsoleLogFunc = new tTJSFunctionWrapper<NeuroConsoleLog>();
    RegisterTJSFunction(global, TJS_W("neuroConsoleLog"), g_NeuroConsoleLogFunc);

    g_NeuroDumpFunc = new tTJSFunctionWrapper<NeuroDump>();
    RegisterTJSFunction(global, TJS_W("neuroDump"), g_NeuroDumpFunc);

    global->Release();

    LogToFile("SUCCESS: All 10 functions registered!\n");
    LogToFile("  - neuroTest()\n");
    LogToFile("  - neuroGetVersion()\n");
    LogToFile("  - neuroEcho(param)\n");
    LogToFile("  - neuroSendData(...)\n");
    LogToFile("  - neuroIsTJSReady()\n");
    LogToFile("  - neuroIsKAGReady()\n");
    LogToFile("  - neuroShowConsole()\n");
    LogToFile("  - neuroExec(code)\n");
    LogToFile("  - neuroConsoleLog(\"text\")\n");
    LogToFile("  - neuroDump(obj)\n");

    // Регистрируем перехват Debug.message -> NeuroConsole
    // ВАЖНО: используем ExecuteTJSAsync т.к. мы в init thread, не в main thread
    LogToFile("Setting up Debug.addLoggingHandler...\n");
    ExecuteTJSAsync(L"Debug.addLoggingHandler(neuroConsoleLog)");
    LogToFile("Debug logging handler registered!\n");

    return true;
}

void UnregisterAllFunctions()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global) {
        // Удаляем logging handler перед удалением функции
        tTJSVariant dummy;
        TVPExecuteExpression(TJS_W("Debug.removeLoggingHandler(neuroConsoleLog)"), &dummy);

        global->DeleteMember(0, TJS_W("neuroTest"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroGetVersion"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroEcho"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroSendData"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroIsTJSReady"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroIsKAGReady"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroShowConsole"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroExec"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroConsoleLog"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroDump"), NULL, global);
        global->Release();
    }
}

// ============================================================================
// InitPlugin - точка входа для self-linking
// Вызывается из proxy DLL вместо прямого V2Link
// ============================================================================

// Forward declaration
extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(iTVPFunctionExporter* exporter);

extern "C" __declspec(dllexport) bool InitPlugin()
{
    LogToFile("\n\n=== InitPlugin called (self-linking) ===\n");
    OutputDebugStringW(L"[NEURO PLUGIN] === InitPlugin called ===\n");

    // Ищем iTVPFunctionExporter в памяти игры
    iTVPFunctionExporter* exporter = WaitForExporterInit(10000);

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

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(iTVPFunctionExporter* exporter)
{
    OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
    LogToFile("\n\n=== V2Link called ===\n");

    if (!exporter) {
        LogToFile("ERROR: exporter is NULL!\n");
        return E_FAIL;
    }

    // Инициализация tp_stub
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

extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink()
{
    OutputDebugStringW(L"[NEURO PLUGIN] V2Unlink called - plugin unloading\n");
    LogToFile("V2Unlink called - cleaning up\n");

    // Останавливаем хоткей поток (join ждёт завершения)
    StopHotkeyThread();

    // Закрываем консоль
    ShutdownNeuroConsole();

    // Восстанавливаем оригинальную процедуру окна
    UnsubclassGameWindow();

    // Удаляем зарегистрированные функции
    UnregisterAllFunctions();

    // Освобождаем ScriptDispatch если был получен
    if (g_ScriptDispatch) {
        g_ScriptDispatch->Release();
        g_ScriptDispatch = nullptr;
    }
    g_TJSReady.store(false);

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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        OutputDebugStringW(L"[NEURO PLUGIN] DLL loaded\n");
        LogToFile("\n\n=== NEURO PLUGIN SESSION STARTED ===\n");
    }
    else if (reason == DLL_PROCESS_DETACH) {
        OutputDebugStringW(L"[NEURO PLUGIN] DLL unloading\n");
        LogToFile("=== PLUGIN SESSION ENDED ===\n");
    }
    return TRUE;
}
