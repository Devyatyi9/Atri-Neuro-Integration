#include <Windows.h>

#include <cwchar>
#include <string>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

namespace {
HMODULE gProxyModule = nullptr;
HWND gGameWindow = nullptr;
WNDPROC gOriginalWndProc = nullptr;

constexpr DWORD kExporterRva = 0x003B4538;
constexpr DWORD kVtableRva = 0x00374A50;
constexpr UINT kBootstrapMessage = WM_APP + 0x4E;

void ReportLastError(wchar_t const *operation) {
	DWORD const error = GetLastError();
	wchar_t message[256];
	swprintf_s(message, L"[Version.dll] %s failed (GetLastError=%lu).\n",
	           operation, error);
	OutputDebugStringW(message);
}

iTVPFunctionExporter *WaitForExporter(DWORD timeoutMs) {
	auto *const imageBase = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
	if (!imageBase) {
		return nullptr;
	}

	auto *const exporter =
	    reinterpret_cast<iTVPFunctionExporter *>(imageBase + kExporterRva);
	void *const expectedVtable = imageBase + kVtableRva;
	DWORD const startedAt = GetTickCount();
	while (GetTickCount() - startedAt < timeoutMs) {
		if (!IsBadReadPtr(exporter, sizeof(void *)) &&
		    *reinterpret_cast<void **>(exporter) == expectedVtable) {
			return exporter;
		}
		Sleep(100);
	}
	return nullptr;
}

BOOL CALLBACK FindGameWindow(HWND window, LPARAM) {
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (processId != GetCurrentProcessId() || !IsWindowVisible(window) ||
	    GetParent(window) != nullptr) {
		return TRUE;
	}

	wchar_t className[256] = {};
	GetClassNameW(window, className, ARRAYSIZE(className));
	if (wcsstr(className, L"TTVPWindowForm") ||
	    wcsstr(className, L"TVPMainWindow")) {
		gGameWindow = window;
		return FALSE;
	}
	return TRUE;
}

void LoadOptionalPatchLoader() {
	constexpr wchar_t kPatchLoaderName[] = L"++version.dll";
	if (GetFileAttributesW(kPatchLoaderName) == INVALID_FILE_ATTRIBUTES) {
		return;
	}
	if (LoadLibraryW(kPatchLoaderName)) {
		OutputDebugStringW(L"[Version.dll] Loaded ++version.dll.\n");
	} else {
		ReportLastError(L"LoadLibraryW(++version.dll)");
	}
}

LRESULT CALLBACK BootstrapWndProc(HWND window,
                                  UINT message,
                                  WPARAM wParam,
                                  LPARAM lParam) {
	if (message != kBootstrapMessage) {
		return CallWindowProcW(gOriginalWndProc, window, message, wParam, lParam);
	}

	try {
		tTJSVariant ignored;
		TVPExecuteExpression(TJS_W("Plugins.link(\"Neuro-Integration.dll\")"),
		                     &ignored);
		OutputDebugStringW(
		    L"[Version.dll] Manager linked Neuro-Integration.dll.\n");
	} catch (...) {
		OutputDebugStringW(L"[Version.dll] Plugins.link failed.\n");
	}
	SetWindowLongPtrW(window, GWLP_WNDPROC,
	                  reinterpret_cast<LONG_PTR>(gOriginalWndProc));
	gGameWindow = nullptr;
	gOriginalWndProc = nullptr;
	TVPUninitImportStub();
	return 0;
}

void Payload() {
	LoadOptionalPatchLoader();

	iTVPFunctionExporter *const exporter = WaitForExporter(10000);
	if (!exporter || !TVPInitImportStub(exporter)) {
		OutputDebugStringW(
		    L"[Version.dll] Bootstrap exporter initialization failed.\n");
		return;
	}

	for (int attempt = 0; attempt < 300 && !gGameWindow; ++attempt) {
		EnumWindows(FindGameWindow, 0);
		if (!gGameWindow) {
			Sleep(100);
		}
	}
	if (!gGameWindow) {
		OutputDebugStringW(
		    L"[Version.dll] Bootstrap game window wait timed out.\n");
		TVPUninitImportStub();
		return;
	}

	// SetWindowLongPtrW stores a WNDPROC as LONG_PTR by Win32 API contract.
	// NOLINTNEXTLINE(performance-no-int-to-ptr)
	gOriginalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
	    gGameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BootstrapWndProc)));
	if (!gOriginalWndProc) {
		ReportLastError(L"SetWindowLongPtrW");
		TVPUninitImportStub();
		return;
	}
	if (!PostMessageW(gGameWindow, kBootstrapMessage, 0, 0)) {
		ReportLastError(L"PostMessageW");
		SetWindowLongPtrW(gGameWindow, GWLP_WNDPROC,
		                  reinterpret_cast<LONG_PTR>(gOriginalWndProc));
		gGameWindow = nullptr;
		gOriginalWndProc = nullptr;
		TVPUninitImportStub();
	}
}
}  // namespace

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
	// The full path is taken from the system,
	// which also eliminates the need for searching
	// and prevents the possibility of a looped loading of the library
	wchar_t systemPath[MAX_PATH];
	UINT const systemPathLength = GetSystemDirectoryW(systemPath, MAX_PATH);
	HMODULE hOriginalDll = NULL;
	if (systemPathLength > 0 && systemPathLength < MAX_PATH) {
		std::wstring path(systemPath, systemPathLength);
		path += L"\\version.dll";
		hOriginalDll = LoadLibraryW(path.c_str());
	}
	if (!hOriginalDll) {
		ReportLastError(L"LoadLibraryW(system32\\version.dll)");
	}
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

BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
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
