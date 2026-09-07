#pragma once

#include <string>

#include "ActionProbes.h"
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

// The same announcement, held until the list stops moving. Turning pages with
// the wheel walks through them one at a time, and announcing each would prompt
// Neuro with pages she is already past.
//
// QueueEnteringContext records what to say and restarts the wait;
// SendPendingContext is called on every refresh and sends it once the wait is
// over; ForgetPendingContext drops it when the screen is gone.
void QueueEnteringContext(Mode mode, int payload);
void SendPendingContext();
void ForgetPendingContext();

// The per-slot edit menu, which takes the screen the way a dialog does: its own
// set, its own line, and a forced choice, because nothing else answers while it
// stands. `payload` carries which of its entries can be used.
void AppendEditMenuActions(ActionSet &set, int payload);
void SendEditMenuContext(int payload);
void ForceEditMenuChoice(int payload);

// True when the payload says the edit menu is what the screen is showing.
bool PayloadHasEditMenu(int payload);

// The tab bar and the page buttons, read into the capability value -- or, while
// the per-slot edit menu is up, that menu and its usable entries instead.
bool ReadPayload(ScreenSnapshot const &screen, int &payload);

}  // namespace ScreenSlots
