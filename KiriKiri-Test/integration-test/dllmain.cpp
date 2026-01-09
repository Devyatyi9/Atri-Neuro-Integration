#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>

// ============================================================================
// KiriKiri SDK - tp_stub (для консоли и TVPExecuteExpression)
// ============================================================================
#include "../krkr-sdk/tp_stub.h"

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

// ============================================================================
// V2Link - регистрация плагина (гибридный подход)
// ============================================================================

extern "C" __declspec(dllexport) HRESULT _stdcall V2Link(iTVPFunctionExporter* exporter)
{
    OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
    LogToFile("\n\n=== V2Link called ===\n");

    if (!exporter) {
        LogToFile("ERROR: exporter is NULL!\n");
        return E_FAIL;
    }

    // Инициализация tp_stub для будущего использования (консоль, TVPExecuteExpression)
    // НЕ вызываем TVPGetScriptDispatch() здесь - TJS ещё не готов!
    if (!TVPInitImportStub(exporter)) {
        LogToFile("ERROR: TVPInitImportStub failed!\n");
        return E_FAIL;
    }
    LogToFile("TVPInitImportStub OK (for later use)\n");

    // Регистрируем функции через exporter->QueryFunctions (работает на ранней стадии)
    const char* functionNames[] = {
        "neuroTest",
        "neuroGetVersion",
        "neuroEcho",
        "neuroSendData"
    };

    void* functionPointers[] = {
        (void*)NeuroTestFunction,
        (void*)NeuroGetVersion,
        (void*)NeuroEcho,
        (void*)NeuroSendData
    };

    LogToFile("Registering functions via exporter...\n");
    bool success = exporter->QueryFunctions(functionNames, functionPointers, 4);

    if (success) {
        OutputDebugStringW(L"[NEURO PLUGIN] All functions registered successfully!\n");
        LogToFile("SUCCESS: All 4 functions registered!\n");
        LogToFile("  - neuroTest()\n");
        LogToFile("  - neuroGetVersion()\n");
        LogToFile("  - neuroEcho(param)\n");
        LogToFile("  - neuroSendData(...)\n");

        MessageBoxW(NULL,
            L"Neuro-integration plugin loaded successfully!\n\n"
            L"Registered functions:\n"
            L"  - neuroTest() - shows test message\n"
            L"  - neuroGetVersion() - returns 100\n"
            L"  - neuroEcho(text) - echoes parameter\n"
            L"  - neuroSendData(...) - for future use\n\n"
            L"tp_stub initialized for console support!",
            L"Neuro Plugin - Ready!",
            MB_OK | MB_ICONINFORMATION);

        return S_OK;
    }

    LogToFile("ERROR: Registration failed!\n");
    OutputDebugStringW(L"[NEURO PLUGIN] Registration failed!\n");

    MessageBoxW(NULL,
        L"Failed to register functions!\n\n"
        L"Check plugin_log.txt for details.",
        L"Neuro Plugin - Error",
        MB_OK | MB_ICONERROR);

    return E_FAIL;
}

// ============================================================================
// V2Unlink - выгрузка плагина
// ============================================================================

extern "C" __declspec(dllexport) HRESULT _stdcall V2Unlink()
{
    OutputDebugStringW(L"[NEURO PLUGIN] V2Unlink called - plugin unloading\n");
    LogToFile("V2Unlink called - cleaning up\n");

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
