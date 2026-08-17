#include <Windows.h>

#include <cwchar>

namespace {
HMODULE gProxyModule = nullptr;
HMODULE gPluginModule = nullptr;

void ReportLastError(wchar_t const *operation) {
	DWORD const error = GetLastError();
	wchar_t message[256];
	swprintf_s(message, L"[Version.dll] %s failed (GetLastError=%lu).\n",
	           operation, error);
	OutputDebugStringW(message);
}
}  // namespace

static void Payload() {
	wchar_t path[32768];
	DWORD const pathLength =
	    GetModuleFileNameW(gProxyModule, path, ARRAYSIZE(path));
	if (pathLength == 0) {
		ReportLastError(L"GetModuleFileNameW");
		return;
	}
	if (pathLength >= ARRAYSIZE(path)) {
		OutputDebugStringW(L"[Version.dll] Proxy module path is truncated.\n");
		return;
	}

	wchar_t *const finalSlash = wcsrchr(path, L'\\');
	if (!finalSlash) {
		OutputDebugStringW(L"[Version.dll] Proxy module path has no directory.\n");
		return;
	}

	constexpr wchar_t kPluginName[] = L"Neuro-Integration.dll";
	size_t const directoryLength = finalSlash - path + 1;
	if (directoryLength + ARRAYSIZE(kPluginName) > ARRAYSIZE(path)) {
		OutputDebugStringW(L"[Version.dll] Plugin path is too long.\n");
		return;
	}
	wcscpy_s(path + directoryLength, ARRAYSIZE(path) - directoryLength,
	         kPluginName);

	HMODULE const plugin = LoadLibraryW(path);
	if (!plugin) {
		ReportLastError(L"LoadLibraryW(Neuro-Integration.dll)");
		return;
	}

	using InitPluginProc = bool(__cdecl *)();
	auto const initPlugin =
	    reinterpret_cast<InitPluginProc>(GetProcAddress(plugin, "InitPlugin"));
	if (!initPlugin) {
		ReportLastError(L"GetProcAddress(InitPlugin)");
		FreeLibrary(plugin);
		return;
	}

	if (!initPlugin()) {
		OutputDebugStringW(L"[Version.dll] InitPlugin returned false.\n");
		FreeLibrary(plugin);
		return;
	}

	gPluginModule = plugin;
	OutputDebugStringW(L"[Version.dll] Neuro-Integration.dll initialized.\n");
}

static DWORD WINAPI PayloadThreadProc(LPVOID) {
	Payload();
	return 0;
}

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

BOOL WINAPI DllMain(HMODULE hModule, DWORD r, LPVOID) {
	if (r == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hModule);
		VersionDllInit();
		gProxyModule = hModule;
		HANDLE thread =
		    CreateThread(nullptr, 0, PayloadThreadProc, nullptr, 0, nullptr);
		if (thread) {
			CloseHandle(thread);
		} else {
			ReportLastError(L"CreateThread");
		}
	}
	return TRUE;
}
