#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "TJSExecutor.h"

// ============================================================================
// NeuroConsole - TJS Debug Console
// ============================================================================
// Окно для ввода и выполнения TJS команд.
// Реализация в NeuroConsole.cpp

// Forward declarations
class NeuroConsole;
class ScriptingWindow;

// Добавить текст в Neuro Console вывод (объявлена в NeuroConsole.cpp)
void AppendToNeuroConsole(wchar_t const *text, unsigned int color = 0x646464);

// Глобальные экземпляры (определёны в NeuroConsole.cpp)
extern std::atomic<NeuroConsole *> g_Console;
// Глобальные переменные для доступа
extern std::atomic<ScriptingWindow *> g_ScriptingWindow;

// Константы для сообщений консоли
constexpr int WM_CONSOLE_TOGGLE = WM_USER + 200;
constexpr int WM_CONSOLE_SHUTDOWN = WM_USER + 201;
constexpr int WM_CONSOLE_APPEND = WM_USER + 202;

// Константы для безопасности
constexpr size_t MAX_CONSOLE_TEXT_LENGTH = 100000;
constexpr size_t MAX_COMMAND_LENGTH = 10000;

// Добавить текст в Scripting Window вывод
void AppendToScriptingWindow(wchar_t const *text,
                             unsigned int color = 0x646464);

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
