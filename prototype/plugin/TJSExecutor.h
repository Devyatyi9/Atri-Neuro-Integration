#pragma once

#include <string>

#include <windows.h>

bool InitializeTJSExecutor();
bool InitializeTJSContinuousEventHook();
// Stops new synchronous calls and releases the ones already waiting. Called
// first thing in shutdown: the game window stops pumping messages as it
// closes, so a call already in WaitForSingleObject would otherwise hold the
// whole timeout, and the thread join behind it holds the process.
void BeginTJSExecutorShutdown();
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
// The answer comes back as UTF-8 text, converted on the game's main thread.
// Deliberately not a tTJSVariant: engine objects are reference-counted over a
// string pool shared with the engine and locked by nothing, so constructing,
// copying or destroying one off the main thread races it. What crosses a thread
// boundary here is plain data.
// `typeName` is optional and receives what TJS calls the result -- "String",
// "Integer", "Object" and so on -- read on the main thread with the value.
// `wrap` off sends the code exactly as written. The wrapper's try/catch lives
// in TJS, so with it the engine never learns an error happened; without it the
// engine reports the error its own way first, and only then the C++ catch in
// the executor keeps the process. That is the whole reason to turn it off --
// seeing what the game would have shown -- and the cost is that the readable
// `!!!TJS_EXCEPTION!!!` text is gone, leaving only a failed call.
bool ExecuteTJSSync(wchar_t const *code,
                    std::string *result,
                    DWORD timeoutMs = 5000,
                    TJSExecMode mode = TJSExecMode::Auto,
                    std::string *typeName = nullptr,
                    bool wrap = true);
