#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class tTJSVariant;

bool InitializeTJSExecutor();
void ShutdownTJSExecutor();
void TryInitTJS();
bool IsTJSReady();
bool IsMainThread();
void ExecuteTJSAsync(wchar_t const *code);
bool ExecuteTJSSync(wchar_t const *code,
                    tTJSVariant *result,
                    DWORD timeoutMs = 5000);
