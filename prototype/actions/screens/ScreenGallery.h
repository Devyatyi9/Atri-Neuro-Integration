#pragma once

#include <string>

#include "ActionSet.h"

// The Extra screen: a nine-square picture grid over several pages, with the
// music list beside it. Everything about it lives here -- what it registers,
// what its actions do, and how its state is read off the screen.
namespace ScreenGallery {

// True when `name` is one of ours and the action has been answered.
bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data);

// Spells the music list out in plain text: only part of it is on screen at a
// time, and the words on its buttons are pictures rather than anything that can
// be read back.
void SendEnteringContext();

// Watches the music list for scrolling nobody told us about -- the player's
// wheel -- and re-sends the list once it stops. The numbers in that list are
// window positions, so a list that is not re-sent after a scroll says the wrong
// thing rather than nothing.
void PollMusicWindow();
void ForgetMusicWindow();

// What the screen offers right now, from the value the refresh stored.
void AppendActions(ActionSet &set, int payload);

// Reads the screen into that value. False when the gallery cannot be read,
// which leaves the caller to resolve the screen some other way.
bool ReadPayload(int &payload);

}  // namespace ScreenGallery
