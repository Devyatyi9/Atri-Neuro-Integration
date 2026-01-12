#pragma once

// ============================================================================
// NeuroConsole - TJS Debug Console
// ============================================================================
// Окно для ввода и выполнения TJS команд.
// Реализация в NeuroConsole.cpp

// Forward declaration класса
class NeuroConsole;

// Глобальный экземпляр консоли (определён в NeuroConsole.cpp)
extern NeuroConsole* g_Console;

// Функции для управления консолью из dllmain.cpp
void InitNeuroConsole();
void ShutdownNeuroConsole();
void ToggleNeuroConsole();
bool IsNeuroConsoleVisible();

// Добавить текст в вывод консоли (для перехвата Debug.message)
void AppendToNeuroConsole(const wchar_t* text, unsigned int color = 0x646464);
