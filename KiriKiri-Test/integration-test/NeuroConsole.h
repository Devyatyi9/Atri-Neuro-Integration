#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "TJSExecutor.h"

// ============================================================================
// NeuroConsole - TJS Debug Console
// ============================================================================
// Window for entering and executing TJS commands.
// Implemented in NeuroConsole.cpp.

// Forward declarations
class NeuroConsole;
class ScriptingWindow;

// Append text to Neuro Console output (defined in NeuroConsole.cpp).
void AppendToNeuroConsole(wchar_t const *text, unsigned int color = 0x646464);

// Global instances (defined in NeuroConsole.cpp).
extern std::atomic<NeuroConsole *> g_Console;
// Global access variables.
extern std::atomic<ScriptingWindow *> g_ScriptingWindow;

// Console message constants.
constexpr int WM_CONSOLE_TOGGLE = WM_USER + 200;
constexpr int WM_CONSOLE_SHUTDOWN = WM_USER + 201;
constexpr int WM_CONSOLE_APPEND = WM_USER + 202;

// Safety constants.
constexpr size_t MAX_CONSOLE_TEXT_LENGTH = 100000;
constexpr size_t MAX_COMMAND_LENGTH = 10000;

// Append text to Scripting Window output.
void AppendToScriptingWindow(wchar_t const *text,
                             unsigned int color = 0x646464);

// Console control functions called from dllmain.cpp.
void InitNeuroConsole();
void ShutdownNeuroConsole();
void ToggleNeuroConsole();
bool IsNeuroConsoleVisible();

// Scripting Window control functions.
void InitScriptingWindow();
void ShutdownScriptingWindow();
void ToggleScriptingWindow();
bool IsScriptingWindowVisible();
