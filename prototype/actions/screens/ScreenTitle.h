#pragma once

#include <string>

#include "ActionProbes.h"
#include "ActionSet.h"

// The title screen and the ways out of it. Four of its entries are always
// usable -- new game, load, settings, quit -- and three depend on what the
// profile has unlocked, which the game shows on the buttons themselves.
namespace ScreenTitle {

// True when `name` is one of ours and the action has been answered.
bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data);

// What the title offers, from the value the refresh stored, and the line that
// says which of it is greyed out.
void AppendActions(ActionSet &set, int payload);
void SendEnteringContext(int payload);

// Whether the title is built yet, and what it is showing. `ready` false means
// the screen exists but its buttons do not, which is still the startup wait.
int ReadPayload(ScreenSnapshot const &screen, bool &ready);

// Continue leaves the title behind only once the scene has loaded, so the
// refresh reports where it is each poll and the flag is cleared here: by the
// screen changing, or by the wait running long enough that the press plainly
// did not take.
void NoteRefresh(bool onTitle);

}  // namespace ScreenTitle
