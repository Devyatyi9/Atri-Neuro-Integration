#include <Windows.h>
#include <thread>

#define WIN32_LEAN_AND_MEAN

// ============================================================================
// Minimal plugin loader (self-linking)
// The plugin now handles all iTVPFunctionExporter lookup logic.
// ============================================================================

void LoadPlugin() {
	OutputDebugStringW(L"[Version.dll] Loading Neuro-Integration.dll...\n");

	HMODULE hPlugin = LoadLibraryW(L"Neuro-Integration.dll");
	if (!hPlugin) {
		wchar_t msg[512];
		swprintf_s(msg, L"Failed to load Neuro-Integration.dll\nError code: %d",
		           GetLastError());
		MessageBoxW(nullptr, msg, L"Plugin Loader Error", MB_OK | MB_ICONERROR);
		OutputDebugStringW(L"[Version.dll] Failed to load Neuro-Integration.dll\n");
		return;
	}

	OutputDebugStringW(L"[Version.dll] Neuro-Integration.dll loaded\n");

	// Call InitPlugin; it finds the exporter and calls V2Link itself.
	typedef bool (*InitPluginFunc)();
	InitPluginFunc initPlugin =
	    (InitPluginFunc)GetProcAddress(hPlugin, "InitPlugin");

	if (!initPlugin) {
		MessageBoxW(nullptr,
		            L"InitPlugin not found in Neuro-Integration.dll\n"
		            L"Make sure your plugin exports InitPlugin function.",
		            L"Plugin Loader Error", MB_OK | MB_ICONERROR);
		OutputDebugStringW(L"[Version.dll] InitPlugin not found in plugin\n");
		FreeLibrary(hPlugin);
		return;
	}

	OutputDebugStringW(L"[Version.dll] Calling InitPlugin...\n");

	if (!initPlugin()) {
		MessageBoxW(nullptr,
		            L"InitPlugin failed!\n"
		            L"Check plugin_log.txt for details.",
		            L"Plugin Loader Error", MB_OK | MB_ICONERROR);
		OutputDebugStringW(L"[Version.dll] InitPlugin failed\n");
		FreeLibrary(hPlugin);
		return;
	}

	OutputDebugStringW(L"[Version.dll] Plugin initialized successfully!\n");
}

// ============================================================================
// version.dll proxying (existing code)
// ============================================================================
#define WRAPPER_GENFUNC(name)        \
	FARPROC orig_##name;               \
	__declspec(naked) void _##name() { \
		__asm jmp[orig_##name]           \
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

#define WRAPPER_FUNC(name)                             \
	if (hOriginalDll) {                                  \
		orig_##name = GetProcAddress(hOriginalDll, #name); \
	} else {                                             \
		orig_##name = NULL;                                \
	}

void VersionDllInit() {
	HMODULE hOriginalDll = LoadLibrary(L"C:\\Windows\\system32\\version.dll");
	if (!hOriginalDll) {
		MessageBoxW(nullptr, L"Failed to load original version.dll!", L"Error",
		            MB_OK | MB_ICONERROR);
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
BOOL WINAPI DllMain(HMODULE hModule, DWORD r, LPVOID) {
	if (r == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hModule);

		OutputDebugStringW(L"[Version.dll] DLL_PROCESS_ATTACH\n");

		// Initialize proxying for the original version.dll.
		VersionDllInit();

		// Load the plugin on a separate thread.
		// Important: detach so the thread runs independently.
		std::thread(LoadPlugin).detach();
	}
	return TRUE;
}
