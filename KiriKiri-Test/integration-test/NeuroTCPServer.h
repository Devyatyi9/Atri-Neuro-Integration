#pragma once

#include <atomic>
#include <string>
#include <thread>

// ============================================================================
// NeuroTCPServer — TCP сервер для удалённого выполнения TJS кода
// ============================================================================
// Позволяет внешним инструментам отправлять TJS команды в плагин.
//
// Протокол:
//   Client → Server: [4-byte LE length][UTF-8 TJS code]
//   Server → Client: [4-byte LE length][UTF-8 result]
//
// Потокобезопасность: использует ExecuteTJSSync() через PostMessage/Subclass.

constexpr int NEURO_TCP_PORT = 18700;
constexpr int NEURO_TCP_MAX_CLIENTS = 4;
constexpr size_t NEURO_TCP_MAX_MSG_SIZE = 65536;

bool StartTCPServer();
void StopTCPServer();
bool IsTCPServerRunning();
