#pragma once

#include <windows.h>

class tTJSVariant;

bool InitializeTJSExecutor();
void ShutdownTJSExecutor();
void StopAcceptingTJSCalls();
void BlockNextTJSSyncCallForStressTest();
void HoldTJSSyncCallsForStressTest();
void TryInitTJS();
bool IsTJSReady();
bool IsMainThread();
void ExecuteTJSAsync(wchar_t const *code);
bool ExecuteTJSSync(wchar_t const *code,
                    tTJSVariant *result,
                    DWORD timeoutMs = 5000);
