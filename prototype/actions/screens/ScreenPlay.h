#pragma once

#include <string>

#include "ActionProbes.h"
#include "ActionSet.h"

// The story itself: advancing the dialogue, answering a choice, and the quick
// menu bar with everything on it.
namespace ScreenPlay {

// The text window hidden away by the bar's hide button. Nothing else is on
// screen in that state, so it is a set of its own rather than a flag.
bool IsTextHidden(ScreenSnapshot const &screen);

// True when `name` is one of ours and the action has been answered.
bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data);

// What the story offers right now, from the value the refresh stored.
void AppendActions(ActionSet &set, int payload);

// The one action left when the interface is hidden.
void AppendHiddenActions(ActionSet &set);

// Everything the quick menu bar carries, as one line of "name -- description"
// pairs. The settings screen offers this the way the game offers its own key
// reference there: a reminder of controls that are not in front of you.
//
// Built from the same list the bar registers, so the two cannot disagree.
std::string QuickMenuReference();

// The choice layer, the bar's position and its buttons, read into the
// capability value.
int ReadPayload(ScreenSnapshot const &screen);

// The two parts of that value the log line names.
bool HasChoice(int payload);
bool HasPanel(int payload);

}  // namespace ScreenPlay
