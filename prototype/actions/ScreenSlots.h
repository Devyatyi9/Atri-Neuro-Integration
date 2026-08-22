#pragma once

#include <string>

#include "ActionSet.h"

// The save screen and its four tabs: Save, Load, Q.load and Voice. One screen
// with one grid of ten slots; which scenario runs behind it decides what a slot
// press does, and the tab bar switches between them.
namespace ScreenSlots {

// Which of the three scenarios is running. The state string does not follow the
// tab bar, so this comes from the conductor.
enum class Mode { Load, Save, Voice };

// Maps `kag.conductor.curStorage` onto a mode. False when this is not one of
// the slot screens at all.
bool ModeForScenario(std::string const &storage, Mode &mode);

// True when `name` is one of ours and the action has been answered.
bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data);

// What the screen offers, and the line describing what is in the ten slots.
// The context is sent from here because it depends on the same reading.
void AppendActions(ActionSet &set, Mode mode, int payload);
void SendEnteringContext(Mode mode, int payload);

// The tab bar and the page buttons, read into the capability value.
bool ReadPayload(int &payload);

}  // namespace ScreenSlots
