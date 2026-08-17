#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);
		OutputDebugStringW(L"[Neuro-Integration] DLL attached.\n");
	} else if (reason == DLL_PROCESS_DETACH) {
		OutputDebugStringW(L"[Neuro-Integration] DLL detached.\n");
	}
	return TRUE;
}
