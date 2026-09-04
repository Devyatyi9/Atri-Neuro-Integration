#pragma once

#include <cstddef>
#include <string>

// Moves an existing log aside into Logs\, named after when it was last written,
// so a new run starts with an empty file. Call it immediately before opening a
// log for the first time in the process.
//
// The logs are opened in append mode and used to grow across every run, which
// made "did this happen just now" unanswerable without comparing timestamps --
// and twice led to reading an old incident as a current one.
void ArchiveExistingLog(wchar_t const *name);

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
