#pragma once

#include <cstddef>
#include <string>

void Log(char const *message);
std::wstring GetPluginLogFile(std::size_t offset,
                              std::size_t count,
                              bool fromEnd);
void CloseLogFile();

void LogTagFile(char const *text);
void CloseTagLogFile();
