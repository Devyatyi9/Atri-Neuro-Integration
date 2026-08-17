#include <windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hModule);
		OutputDebugStringW(L"[NEURO PLUGIN] DLL loaded\n");
	} else if (reason == DLL_PROCESS_DETACH) {
		OutputDebugStringW(L"[NEURO PLUGIN] DLL unloading\n");
	}
	return TRUE;
}
