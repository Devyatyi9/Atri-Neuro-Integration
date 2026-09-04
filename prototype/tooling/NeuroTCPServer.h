#pragma once

#include <thread>

// ============================================================================
// NeuroTCPServer: TCP server for remote TJS code execution.
// ============================================================================
// Allows external tools to send TJS commands to the plugin.
//
// Protocol:
//   Client → Server: [4-byte LE length][UTF-8 TJS code]
//   Server → Client: [4-byte LE length][UTF-8 result]
//
// Thread safety: uses ExecuteTJSSync(), which hands the call to the game's own
// continuous event hook and waits for the main thread to run it.

// The default only. The port in use comes from the plugin config
// (PluginConfig.h), because it is machine-wide state: two copies of the game
// cannot both have it.
constexpr int NEURO_TCP_PORT = 18700;
constexpr int NEURO_TCP_MAX_CLIENTS = 4;
constexpr size_t NEURO_TCP_MAX_MSG_SIZE = 65536;

bool StartTCPServer();
void StopTCPServer();
bool IsTCPServerRunning();
void ScheduleExitAfterTCPResponse();
