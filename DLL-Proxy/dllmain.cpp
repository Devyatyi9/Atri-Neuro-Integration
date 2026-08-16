#include <Windows.h>

void Payload() {
	HMODULE hModule = LoadLibraryW(L"Neuro-integration.dll");
	if (hModule != NULL) {
		OutputDebugStringW(
		    L"[Version.dll] Neuro-integration.dll loaded successfully.\n");
	} else {
	}
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
		Payload();
	}
	return TRUE;
}
