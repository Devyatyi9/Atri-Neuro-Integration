#include <Windows.h>
#include <thread>

// ============================================================================
// Интерфейс iTVPFunctionExporter
// ============================================================================
class iTVPFunctionExporter {
public:
    virtual ~iTVPFunctionExporter() {}
    // Виртуальные методы здесь, но для нас важен только указатель на vtable
};

// ============================================================================
// Загрузка плагина через iTVPFunctionExporter
// ============================================================================
iTVPFunctionExporter* GetFunctionExporterFromMemory() {
    HMODULE hMain = GetModuleHandle(nullptr);
    if (!hMain) return nullptr;

    BYTE* baseAddress = (BYTE*)hMain;
    const DWORD RVA_EXPORTER = 0x003B4538;

    iTVPFunctionExporter* exporter = (iTVPFunctionExporter*)(baseAddress + RVA_EXPORTER);

    if (!IsBadReadPtr(exporter, sizeof(void*))) {
        void** vtable = *(void***)exporter;
        if (vtable && !IsBadReadPtr(vtable, sizeof(void*))) {
            return exporter;
        }
    }

    return nullptr;
}

iTVPFunctionExporter* WaitForExporterInit(int maxWaitMs = 10000) {
    int elapsed = 0;
    const int checkInterval = 100;

    while (elapsed < maxWaitMs) {
        iTVPFunctionExporter* exporter = GetFunctionExporterFromMemory();
        if (exporter) {
            return exporter;
        }

        Sleep(checkInterval);
        elapsed += checkInterval;
    }

    return nullptr;
}

void LoadAndLinkPlugin() {
    // Ждём инициализации движка
    iTVPFunctionExporter* exporter = WaitForExporterInit(10000);

    if (!exporter) {
        MessageBoxW(nullptr,
            L"Failed to get iTVPFunctionExporter after 10 seconds.\n"
            L"Address: 0x007B4538 (RVA: 0x003B4538)\n"
            L"VTable should be at: 0x00774A50 (RVA: 0x00374A50)",
            L"Plugin Loader Error", MB_OK | MB_ICONERROR);
        return;
    }

    OutputDebugStringW(L"[Version.dll] iTVPFunctionExporter found!\n");

    // Загружаем ваш плагин
    HMODULE hPlugin = LoadLibraryW(L"Neuro-integration.dll");
    if (!hPlugin) {
        wchar_t msg[512];
        swprintf_s(msg, L"Failed to load Neuro-integration.dll\nError code: %d", GetLastError());
        MessageBoxW(nullptr, msg, L"Plugin Loader Error", MB_OK | MB_ICONERROR);
        OutputDebugStringW(L"[Version.dll] Failed to load Neuro-integration.dll\n");
        return;
    }

    OutputDebugStringW(L"[Version.dll] Neuro-integration.dll loaded\n");

    // Получаем V2Link из плагина
    typedef HRESULT(__stdcall* V2LinkFunc)(iTVPFunctionExporter*);
    V2LinkFunc v2link = (V2LinkFunc)GetProcAddress(hPlugin, "V2Link");

    if (!v2link) {
        MessageBoxW(nullptr,
            L"V2Link not found in Neuro-integration.dll\n"
            L"Make sure your plugin exports V2Link function.",
            L"Plugin Loader Error", MB_OK | MB_ICONERROR);
        OutputDebugStringW(L"[Version.dll] V2Link not found in plugin\n");
        FreeLibrary(hPlugin);
        return;
    }

    OutputDebugStringW(L"[Version.dll] V2Link found, calling it...\n");

    // Вызываем V2Link, связывая плагин с движком!
    HRESULT hr = v2link(exporter);

    if (FAILED(hr)) {
        wchar_t msg[512];
        swprintf_s(msg, L"V2Link failed with HRESULT: 0x%08X", hr);
        MessageBoxW(nullptr, msg, L"Plugin Loader Error", MB_OK | MB_ICONERROR);
        OutputDebugStringW(L"[Version.dll] V2Link failed\n");
        FreeLibrary(hPlugin);
        return;
    }

    OutputDebugStringW(L"[Version.dll] Plugin successfully loaded and linked!\n");
    MessageBoxW(nullptr,
        L"Plugin successfully loaded and linked!\n"
        L"iTVPFunctionExporter: 0x007B4538\n"
        L"VTable: 0x00774A50",
        L"Plugin Loader Success", MB_OK | MB_ICONINFORMATION);
}

// ============================================================================
// Проксирование version.dll (ваш существующий код)
// ============================================================================
#define WRAPPER_GENFUNC(name) \
	FARPROC orig_##name; \
	__declspec(naked) void _##name() \
	{ \
		__asm jmp[orig_##name] \
	}

WRAPPER_GENFUNC(GetFileVersionInfoA)
WRAPPER_GENFUNC(GetFileVersionInfoByHandle)
WRAPPER_GENFUNC(GetFileVersionInfoExW)
WRAPPER_GENFUNC(GetFileVersionInfoExA)
WRAPPER_GENFUNC(GetFileVersionInfoSizeA)
WRAPPER_GENFUNC(GetFileVersionInfoSizeExA)
WRAPPER_GENFUNC(GetFileVersionInfoSizeExW)
WRAPPER_GENFUNC(GetFileVersionInfoSizeW)
WRAPPER_GENFUNC(GetFileVersionInfoW)
WRAPPER_GENFUNC(VerFindFileA)
WRAPPER_GENFUNC(VerFindFileW)
WRAPPER_GENFUNC(VerInstallFileA)
WRAPPER_GENFUNC(VerInstallFileW)
WRAPPER_GENFUNC(VerLanguageNameA)
WRAPPER_GENFUNC(VerLanguageNameW)
WRAPPER_GENFUNC(VerQueryValueA)
WRAPPER_GENFUNC(VerQueryValueW)

#define WRAPPER_FUNC(name) \
    if (hOriginalDll) { \
        orig_##name = GetProcAddress(hOriginalDll, #name); \
    } else { \
        orig_##name = NULL; \
    }

void VersionDllInit()
{
    HMODULE hOriginalDll = LoadLibrary(L"C:\\Windows\\system32\\version.dll");
    if (!hOriginalDll) {
        MessageBoxW(nullptr, L"Failed to load original version.dll!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    OutputDebugStringW(L"[Version.dll] Original version.dll loaded\n");

    WRAPPER_FUNC(GetFileVersionInfoA);
    WRAPPER_FUNC(GetFileVersionInfoByHandle);
    WRAPPER_FUNC(GetFileVersionInfoExW);
    WRAPPER_FUNC(GetFileVersionInfoExA);
    WRAPPER_FUNC(GetFileVersionInfoSizeA);
    WRAPPER_FUNC(GetFileVersionInfoSizeExW);
    WRAPPER_FUNC(GetFileVersionInfoSizeExA);
    WRAPPER_FUNC(GetFileVersionInfoSizeW);
    WRAPPER_FUNC(GetFileVersionInfoW);
    WRAPPER_FUNC(VerFindFileA);
    WRAPPER_FUNC(VerFindFileW);
    WRAPPER_FUNC(VerInstallFileA);
    WRAPPER_FUNC(VerInstallFileW);
    WRAPPER_FUNC(VerLanguageNameA);
    WRAPPER_FUNC(VerLanguageNameW);
    WRAPPER_FUNC(VerQueryValueA);
    WRAPPER_FUNC(VerQueryValueW);
}

// ============================================================================
// DllMain
// ============================================================================
BOOL WINAPI DllMain(HMODULE hModule, DWORD r, LPVOID)
{
    if (r == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        OutputDebugStringW(L"[Version.dll] DLL_PROCESS_ATTACH\n");

        // Инициализируем проксирование оригинальной version.dll
        VersionDllInit();

        // Запускаем загрузку плагина в отдельном потоке
        // ВАЖНО: используем detach() чтобы поток работал независимо
        std::thread(LoadAndLinkPlugin).detach();
    }
    return TRUE;
}