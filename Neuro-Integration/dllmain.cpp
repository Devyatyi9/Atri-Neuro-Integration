#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <neurosdk.h>

DWORD WINAPI InitThreadProc(LPVOID lpParameter){
    MessageBox(NULL, L"Hello from Neuro integration dll", L"Neuro-integration", MB_OK);
    //neurosdk_context_t ctx;
    return FALSE;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule); // not necessarily
        HANDLE hThread = CreateThread(NULL, 0, InitThreadProc, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        break;
    default:
		break;
    }
    return TRUE;
}

