#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <thread>
#include <atomic>

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
static iTJSDispatch2* g_ScriptDispatch = nullptr;

// ============================================================================
// Логирование
// ============================================================================

void LogToFile(const char* message) {
    HANDLE hFile = CreateFileW(L"plugin_log.txt",
        GENERIC_WRITE, FILE_SHARE_READ, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        SetFilePointer(hFile, 0, NULL, FILE_END);
        WriteFile(hFile, message, (DWORD)strlen(message), &written, NULL);
        CloseHandle(hFile);
    }
}

// ============================================================================
// Отложенная инициализация TJS
// ============================================================================

void TryInitTJS() {
    if (g_TJSReady.load()) return;

    iTJSDispatch2* dispatch = TVPGetScriptDispatch();
    if (dispatch) {
        g_ScriptDispatch = dispatch;
        g_TJSReady.store(true);
        LogToFile("TJS initialized successfully!\n");
        OutputDebugStringW(L"[NEURO PLUGIN] TJS initialized!\n");
    }
}

// Forward declaration
bool RegisterAllFunctions();

void DeferredTJSInitThread() {
    LogToFile("Deferred TJS init thread started...\n");

    const int maxAttempts = 100;  // 10 секунд максимум
    const int intervalMs = 100;

    for (int i = 0; i < maxAttempts && !g_TJSReady.load(); i++) {
        Sleep(intervalMs);
        TryInitTJS();
    }

    if (g_TJSReady.load()) {
        LogToFile("Deferred TJS init: SUCCESS\n");

        // Регистрируем функции в TJS
        if (RegisterAllFunctions()) {
            OutputDebugStringW(L"[NEURO PLUGIN] Functions registered successfully!\n");

            MessageBoxW(NULL,
                L"Neuro-integration plugin ready!\n\n"
                L"Registered functions:\n"
                L"  - neuroTest()\n"
                L"  - neuroGetVersion()\n"
                L"  - neuroEcho(text)\n"
                L"  - neuroSendData(...)\n"
                L"  - neuroIsTJSReady()\n"
                L"  - neuroExec(code)",
                L"Neuro Plugin - Ready!",
                MB_OK | MB_ICONINFORMATION);
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
static tTJSFunctionWrapper<NeuroExec>* g_NeuroExecFunc = nullptr;

void RegisterTJSFunction(iTJSDispatch2* global, const tjs_char* name, tTJSDispatch* func)
{
    tTJSVariant val(func);
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

    g_NeuroExecFunc = new tTJSFunctionWrapper<NeuroExec>();
    RegisterTJSFunction(global, TJS_W("neuroExec"), g_NeuroExecFunc);

    global->Release();

    LogToFile("SUCCESS: All 6 functions registered!\n");
    LogToFile("  - neuroTest()\n");
    LogToFile("  - neuroGetVersion()\n");
    LogToFile("  - neuroEcho(param)\n");
    LogToFile("  - neuroSendData(...)\n");
    LogToFile("  - neuroIsTJSReady()\n");
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
