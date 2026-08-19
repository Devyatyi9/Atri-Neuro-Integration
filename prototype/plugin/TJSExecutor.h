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
// How a snippet reaches the engine. TJS has two distinct entry points and they
// are not interchangeable: an expression yields a value but cannot contain
// statements, while a script runs statements but only yields a value through a
// top-level `return`.
//   Expression - TVPExecuteExpression; `1 + 1`, `typeof kag`, a single call.
//   Script     - TVPExecuteScript; `if (...) { ... }`, `var x = 1; ...`.
//   Auto       - pick by scanning for a top-level statement separator. Intended
//                for interactive input (TCP console) where the caller cannot
//                know. Prefer an explicit mode in code: the scan cannot tell a
//                statement from an expression when the whole snippet is one
//                `if (...) { ... }` block, whose separators all sit inside the
//                braces.
enum class TJSExecMode { Auto, Expression, Script };

void ExecuteTJSAsync(wchar_t const *code, TJSExecMode mode = TJSExecMode::Auto);
bool ExecuteTJSSync(wchar_t const *code,
                    tTJSVariant *result,
                    DWORD timeoutMs = 5000,
                    TJSExecMode mode = TJSExecMode::Auto);
