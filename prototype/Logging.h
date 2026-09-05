#pragma once

#include <cstddef>
#include <string>

// A ring of the plugin's own diagnostic lines, in memory and nowhere else.
// Nothing is written to disk: the lines exist to be read back from the console
// while the game runs, and they go with the process.

void Log(char const *message);

// A window onto the ring, oldest first. `fromEnd` counts `offset` back from the
// newest line instead of forward from the oldest.
std::wstring GetPluginLog(std::size_t offset, std::size_t count, bool fromEnd);

// Drops everything the ring holds.
void ClearPluginLog();
