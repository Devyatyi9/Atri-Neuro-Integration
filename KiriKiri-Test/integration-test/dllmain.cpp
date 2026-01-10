#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>

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

    // Subclass окно
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
bool ExecuteTJSSync(const wchar_t* code, tTJSVariant* result, DWORD timeoutMs = 5000) {
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
            LogToFile("WARNING: Window subclass failed, hotkeys will use direct calls\n");
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
            LogToFile("Hotkeys: F9=console, F10=test, F12=KAG check\n");

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
    LogToFile("Hotkey thread started (F9 = console, F10 = test, F12 = KAG)\n");
    LogToFile("  Using thread-safe ExecuteTJSAsync/Sync for TJS calls\n");

    while (g_HotkeyThreadRunning.load()) {
        // F9 - показать консоль (синхронный вызов для получения результата)
        if (GetAsyncKeyState(VK_F9) & 0x8000) {
            if (g_TJSReady.load() && g_GameWindow) {
                LogToFile("F9 pressed - showing console (sync)\n");
                tTJSVariant result;

                // Thread-safe синхронный вызов
                if (ExecuteTJSSync(L"neuroShowConsole()", &result)) {
                    const char* msg = (tTVInteger)result ?
                        "Debug.console.visible = true\n(Console may be disabled in this build)" :
                        "Debug.console not available";
                    MessageBoxA(NULL, msg, "Neuro Plugin - F9", MB_OK | MB_ICONINFORMATION);
                } else {
                    LogToFile("F9: ExecuteTJSSync timed out\n");
                    MessageBoxA(NULL, "TJS call timed out", "Neuro Plugin - F9", MB_OK | MB_ICONWARNING);
                }
            }
            Sleep(300);  // Debounce
        }

        // F10 - тест (просто MessageBox, не требует TJS)
        if (GetAsyncKeyState(VK_F10) & 0x8000) {
            if (g_TJSReady.load()) {
                LogToFile("F10 pressed - test\n");
                MessageBoxA(NULL, "F10 hotkey works!\nThread-safe TJS calls enabled.", "Neuro Plugin - F10", MB_OK | MB_ICONINFORMATION);
            }
            Sleep(300);  // Debounce
        }

        // F12 - проверить KAG (синхронный вызов с результатом)
        if (GetAsyncKeyState(VK_F12) & 0x8000) {
            if (g_TJSReady.load() && g_GameWindow) {
                LogToFile("F12 pressed - checking KAG (sync)\n");
                tTJSVariant result;

                // Thread-safe синхронный вызов
                if (ExecuteTJSSync(L"neuroIsKAGReady()", &result)) {
                    char buf[64];
                    sprintf_s(buf, "KAG ready: %s", (tTVInteger)result ? "YES" : "NO");
                    LogToFile(buf);
                    LogToFile("\n");
                    MessageBoxA(NULL, buf, "Neuro Plugin - F12", MB_OK | MB_ICONINFORMATION);
                } else {
                    LogToFile("F12: ExecuteTJSSync timed out\n");
                    MessageBoxA(NULL, "TJS call timed out", "Neuro Plugin - F12", MB_OK | MB_ICONWARNING);
                }
            }
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

    global->Release();

    LogToFile("SUCCESS: All 8 functions registered!\n");
    LogToFile("  - neuroTest()\n");
    LogToFile("  - neuroGetVersion()\n");
    LogToFile("  - neuroEcho(param)\n");
    LogToFile("  - neuroSendData(...)\n");
    LogToFile("  - neuroIsTJSReady()\n");
    LogToFile("  - neuroIsKAGReady()\n");
    LogToFile("  - neuroShowConsole()\n");
    LogToFile("  - neuroExec(code)\n");

    return true;
}

void UnregisterAllFunctions()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global) {
        global->DeleteMember(0, TJS_W("neuroTest"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroGetVersion"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroEcho"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroSendData"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroIsTJSReady"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroIsKAGReady"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroShowConsole"), NULL, global);
        global->DeleteMember(0, TJS_W("neuroExec"), NULL, global);
        global->Release();
    }
}

// ============================================================================
// V2Link - регистрация плагина
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
