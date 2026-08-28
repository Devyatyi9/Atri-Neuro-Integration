#pragma once

#include <cstddef>
#include <string>

void Log(char const *message);
std::wstring GetPluginLogFile(std::size_t offset,
                              std::size_t count,
                              bool fromEnd);
void CloseLogFile();
// Pushes what is buffered to disk without closing, and gives up rather than
// waiting if the lock is held. Meant for a dying process: the thread that is
// about to take everything down may be the one holding the lock, and blocking
// there would lose the very line worth keeping.
void FlushLogFileIfFree();

void LogTagFile(char const *text);
void CloseTagLogFile();
