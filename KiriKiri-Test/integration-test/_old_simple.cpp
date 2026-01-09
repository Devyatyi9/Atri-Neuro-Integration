// main.cpp
#include <windows.h>
#include <string>

// Если у вас нет tp_stub.h, можно временно обойтись без TVPInitImportStub
// Но для реального плагина он потребуется

extern "C" __declspec(dllexport) HRESULT V2Link(void* exporter)
{
    // Простейшая проверка: записать в лог или показать сообщение
    // Вариант 1: MessageBox (для теста, но может блокировать)
    // MessageBoxA(NULL, "V2Link called! Plugin loaded successfully.", 
    //            "Test Plugin", MB_OK | MB_ICONINFORMATION);

    // Вариант 2: OutputDebugString (лучше, не блокирует)
    OutputDebugStringW(L"[TEST PLUGIN] V2Link called successfully!");

    // Вариант 3: Запись в файл (самый надёжный для отладки)
    HANDLE hFile = CreateFileW(L"plugin_log.txt",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "V2Link called\n";
        DWORD written;
        SetFilePointer(hFile, 0, NULL, FILE_END);
        WriteFile(hFile, msg, strlen(msg), &written, NULL);
        CloseHandle(hFile);
    }

    // В реальном плагине здесь будет:
    // TVPInitImportStub(exporter);
    // и инициализация ваших классов

    return S_OK; // Успех
}

extern "C" __declspec(dllexport) HRESULT V2Unlink()
{
    OutputDebugStringW(L"[TEST PLUGIN] V2Unlink called, plugin unloading.");

    // Запись в файл
    HANDLE hFile = CreateFileW(L"plugin_log.txt",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "V2Unlink called\n";
        DWORD written;
        SetFilePointer(hFile, 0, NULL, FILE_END);
        WriteFile(hFile, msg, strlen(msg), &written, NULL);
        CloseHandle(hFile);
    }

    return S_OK;
}

// Минимальный DllMain
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        OutputDebugStringW(L"[TEST PLUGIN] DLL loaded in process.");
    }
    return TRUE;
}