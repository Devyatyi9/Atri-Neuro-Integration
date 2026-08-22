#pragma once

#include <string>

#include "ActionSet.h"

// The backlog: the log of lines already read, its two views, and the per-row
// controls that jump back to a line, replay its voice or keep it.
namespace ScreenBacklog {

// True when `name` is one of ours and the action has been answered.
bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data);

// What the screen offers, from the value the refresh stored.
void AppendActions(ActionSet &set, int payload);

// How long the history is, said once on the way in: the flags change on every
// scroll, the total does not.
void SendEnteringContext();

// The scroll controls and the per-row buttons, read into the capability value.
bool ReadPayload(int &payload);

}  // namespace ScreenBacklog
