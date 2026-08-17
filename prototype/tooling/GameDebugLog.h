#pragma once

#include <cstddef>
#include <string>

void AppendGameDebugLog(std::wstring const &line);
std::wstring GetGameDebugLog(std::size_t offset,
                             std::size_t count,
                             bool fromEnd);
void ClearGameDebugLog();
void CloseGameDebugLogFile();
