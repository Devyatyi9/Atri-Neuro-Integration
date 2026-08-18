#pragma once

#include <string>

// Action handlers run synchronously on the Neuro polling thread. The string
// references remain valid only for the duration of the callback.
using NeuroActionHandler = void (*)(std::string const &id,
                                    std::string const &name,
                                    std::string const &data);

void NeuroSetActionHandler(NeuroActionHandler handler);

bool NeuroConnect(char const *url, char const *gameName);
void NeuroAutoConnect();
void NeuroDisconnect();
bool NeuroIsConnected();
bool NeuroIsReady();

bool NeuroSendContext(char const *message, bool silent);
bool NeuroRegisterAction(char const *name,
                         char const *description,
                         char const *jsonSchema);
bool NeuroUnregisterAction(char const *name);
bool NeuroReregisterActions();
bool NeuroSendActionResult(char const *id, bool success, char const *message);
bool NeuroForceAction(char const *state,
                      char const *query,
                      char const **actionNames,
                      int actionCount,
                      bool ephemeral);
