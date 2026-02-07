#pragma once

#include <windows.h>
#include <vector>
#include <mutex>
#include <atomic>

// ============================================================================
// NeuroConsole - TJS Debug Console
// ============================================================================
// Окно для ввода и выполнения TJS команд.
// Реализация в NeuroConsole.cpp

// Forward declarations
class NeuroConsole;
class ScriptingWindow;
class tTJSVariant;

// Глобальные экземпляры (определёны в NeuroConsole.cpp)
extern std::atomic<NeuroConsole*> g_Console;
extern std::atomic<ScriptingWindow*> g_ScriptingWindow;

// Константы для сообщений консоли
constexpr int WM_CONSOLE_TOGGLE = WM_USER + 200;
constexpr int WM_CONSOLE_SHUTDOWN = WM_USER + 201;
constexpr int WM_CONSOLE_APPEND = WM_USER + 202;

// Константы для безопасности
constexpr size_t MAX_CONSOLE_TEXT_LENGTH = 100000;
constexpr size_t MAX_COMMAND_LENGTH = 10000;

// Функции для управления консолью из dllmain.cpp
void InitNeuroConsole();
void ShutdownNeuroConsole();
void ToggleNeuroConsole();
bool IsNeuroConsoleVisible();

// Функции для управления Scripting Window
void InitScriptingWindow();
void ShutdownScriptingWindow();
void ToggleScriptingWindow();
bool IsScriptingWindowVisible();

// Добавить текст в вывод консоли (для перехвата Debug.message)
void AppendToNeuroConsole(const wchar_t* text, unsigned int color = 0x646464);

// Thread-safe TJS execution (определена в dllmain.cpp)
bool ExecuteTJSSync(const wchar_t* code, tTJSVariant* result, unsigned long timeoutMs = 5000);

// Новые константы для Scripting Window
constexpr int WM_SCRIPTING_TOGGLE = WM_USER + 203;
constexpr int WM_SCRIPTING_APPEND = WM_USER + 204;
