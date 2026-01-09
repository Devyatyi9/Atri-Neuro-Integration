#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdint>
#include <cstdio>

// ============================================================================
// Определения типов KiriKiri
// ============================================================================

typedef int tjs_error;
typedef int tjs_int;
typedef unsigned int tjs_uint;
typedef std::int64_t tjs_int64;
typedef double tjs_real;
typedef wchar_t tjs_char;

#define TJS_S_OK 0
#define TJS_E_FAIL 1
#define TJS_INTF_METHOD __cdecl

class iTJSDispatch2;

struct tTJSVariant {
    enum tTJSVariantType {
        tvtVoid = 0,
        tvtObject = 1,
        tvtString = 2,
        tvtOctet = 3,
        tvtInteger = 4,
        tvtReal = 5
    } Type;

    union {
        tjs_int64 AsInteger;
        tjs_real AsReal;
        void* AsObject;
        const tjs_char* AsString;
    };

    tTJSVariant() : Type(tvtVoid), AsInteger(0) {}

    void operator=(const tjs_char* str) {
        Type = tvtString;
        AsString = str;
    }

    void operator=(tjs_int val) {
        Type = tvtInteger;
        AsInteger = val;
    }
};

// ПРАВИЛЬНЫЙ интерфейс iTVPFunctionExporter для ATRI
// QueryFunctionsByNarrowString находится на индексе 3!
class iTVPFunctionExporter {
public:
    // Индекс 0 - неизвестный метод
    virtual bool TJS_INTF_METHOD UnknownMethod0() = 0;

    // Индекс 1 - неизвестный метод
    virtual bool TJS_INTF_METHOD UnknownMethod1() = 0;

    // Индекс 2 - неизвестный метод (вызывает exception, возможно деструктор или RTTI)
    virtual void TJS_INTF_METHOD UnknownMethod2() = 0;

    // Индекс 3 - QueryFunctionsByNarrowString (рабочий!)
    virtual bool TJS_INTF_METHOD QueryFunctionsByNarrowString(
        const char** name,
        void** function,
        tjs_uint count
    ) = 0;
};

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
// ВАШИ ФУНКЦИИ ДЛЯ TJS
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
        L"🎉 SUCCESS! 🎉\n\n"
        L"NeuroTestFunction was successfully called from TJS!\n\n"
        L"Your Neuro-integration plugin is fully working!\n\n"
        L"You can now implement any functionality you want!",
        L"Neuro Plugin - Function Called!",
        MB_OK | MB_ICONINFORMATION);

    if (result) {
        static const tjs_char response[] = L"Hello from Neuro-integration plugin! Everything works perfectly!";
        *result = response;
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
        *result = (tjs_int)100; // Version 1.0.0
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

        if (firstParam->Type == tTJSVariant::tvtString && firstParam->AsString) {
            // Выводим переданную строку
            MessageBoxW(NULL, firstParam->AsString, L"Neuro Echo", MB_OK | MB_ICONINFORMATION);

            // Возвращаем её обратно
            if (result) {
                *result = firstParam->AsString;
            }
        }
        else if (firstParam->Type == tTJSVariant::tvtInteger) {
            // Если передано число
            wchar_t buffer[128];
            wsprintf(buffer, L"Number received: %I64d", firstParam->AsInteger);
            MessageBoxW(NULL, buffer, L"Neuro Echo", MB_OK | MB_ICONINFORMATION);

            if (result) {
                *result = (tjs_int)firstParam->AsInteger;
            }
        }
    }

    return TJS_S_OK;
}

// Дополнительная функция - обмен данными
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

    // Здесь вы можете обрабатывать данные из игры
    // Например, отправлять их во внешнюю систему, в веб-сервис и т.д.

    if (result) {
        *result = (tjs_int)1; // Успешно отправлено
    }

    return TJS_S_OK;
}

// ============================================================================
// V2Link - регистрация плагина (ФИНАЛЬНАЯ ВЕРСИЯ)
// ============================================================================

extern "C" __declspec(dllexport) HRESULT V2Link(iTVPFunctionExporter* exporter)
{
    OutputDebugStringW(L"[NEURO PLUGIN] === V2Link called ===\n");
    LogToFile("\n\n=== V2Link called ===\n");

    if (!exporter) {
        LogToFile("ERROR: exporter is NULL!\n");
        return E_FAIL;
    }

    // Регистрируем функции
    const char* functionNames[] = {
        "neuroTest",       // Тестовая функция с MessageBox
        "neuroGetVersion", // Возвращает версию плагина
        "neuroEcho",       // Эхо - выводит переданный параметр
        "neuroSendData"    // Отправка данных (для будущего функционала)
    };

    void* functionPointers[] = {
        (void*)NeuroTestFunction,
        (void*)NeuroGetVersion,
        (void*)NeuroEcho,
        (void*)NeuroSendData
    };

    bool success = exporter->QueryFunctionsByNarrowString(
        functionNames,
        functionPointers,
        4  // 4 функции
    );

    if (success) {
        OutputDebugStringW(L"[NEURO PLUGIN] All functions registered successfully!\n");
        LogToFile("SUCCESS: All 4 functions registered!\n");
        LogToFile("  - neuroTest()\n");
        LogToFile("  - neuroGetVersion()\n");
        LogToFile("  - neuroEcho(param)\n");
        LogToFile("  - neuroSendData(...)\n");

        MessageBoxW(NULL,
            L"✅ Neuro-integration plugin loaded successfully!\n\n"
            L"Registered functions:\n"
            L"  • neuroTest() - shows test message\n"
            L"  • neuroGetVersion() - returns 100\n"
            L"  • neuroEcho(text) - echoes parameter\n"
            L"  • neuroSendData(...) - for future use\n\n"
            L"You can now call these from TJS scripts!",
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

extern "C" __declspec(dllexport) HRESULT V2Unlink()
{
    OutputDebugStringW(L"[NEURO PLUGIN] V2Unlink called - plugin unloading\n");
    LogToFile("V2Unlink called - cleaning up\n");

    // Здесь освобождаем ресурсы если нужно

    return S_OK;
}

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