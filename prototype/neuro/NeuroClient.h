#pragma once

#include <atomic>

#include <string>

// Action handlers run synchronously on the Neuro polling thread. The string
// references remain valid only for the duration of the callback.
using NeuroActionHandler = void (*)(std::string const &id,
                                    std::string const &name,
                                    std::string const &data);

void NeuroSetActionHandler(NeuroActionHandler handler);

// Called on the polling thread each time a session is established, including
// every reconnect after a drop. Whatever a caller remembers about the server it
// was talking to belongs to that session and not to the next one: the new
// server never saw the actions it was sent, the forces it was owed, or the ids
// it handed out.
using NeuroSessionHandler = void (*)();
void NeuroSetSessionHandler(NeuroSessionHandler handler);

bool NeuroConnect(char const *url, char const *gameName);
void NeuroAutoConnect();
void NeuroDisconnect();
bool NeuroIsConnected();
bool NeuroIsReady();

bool NeuroSendContext(char const *message, bool silent);

// Replaces the entire registered action set and queues one ordered snapshot:
// removed names are unregistered first, then the full new set is registered.
// The return value means the snapshot was accepted by the local queue.
struct NeuroRegisteredAction {
	char const *name;
	char const *description;
	char const *jsonSchema;
};
bool NeuroSetRegisteredActions(NeuroRegisteredAction const *actions,
                               int actionCount);
bool NeuroRegisterAction(char const *name,
                         char const *description,
                         char const *jsonSchema);
bool NeuroUnregisterAction(char const *name);
bool NeuroReregisterActions();
// Whether the name is in the set as it was last sent. For reporting only: an
// action that arrives without being offered is answered exactly as any other,
// because the buttons behind the quick menu are reachable whether or not the
// bar is out, the way a player's shortcut keys are.
bool NeuroActionIsRegistered(char const *name);
bool NeuroSendActionResult(char const *id, bool success, char const *message);
// Set by the call above. One action is handled at a time, so this describes the
// action that has just been answered.
extern std::atomic<bool> g_LastActionResultSucceeded;
bool NeuroForceAction(char const *state,
                      char const *query,
                      char const **actionNames,
                      int actionCount,
                      bool ephemeral);
