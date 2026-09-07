#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Pieces the screen modules share.
//
// Each screen owns its own actions, but a few things are the same wherever they
// are used: pressing a control the screen carries, building a bounded schema,
// and the two waits the game imposes on us.

// How long the game takes to settle after presses it does not acknowledge.
// Measured against the save list: the page swap and the confirmation dialog
// both land late, and neither has a readable "ready" flag to poll instead.
constexpr DWORD kPageChangeSettleMs = 1200;
constexpr DWORD kDialogSettleMs = 150;
// How long the backlog's jump preview stays up before the row is pressed. A
// player sees it while moving the cursor; without the pause the confirmation
// would open over a preview that never rendered.
constexpr DWORD kHoverPreviewMs = 400;

// Whether a press should also put the game's own pointer on the target first.
//
// The game has two ways to do it -- setFocusToLink on a dialog, focus() on a
// choice button -- and both move the real mouse onto the control, which is what
// makes a press look like someone playing rather than a value changing. It also
// takes the mouse away from whoever else is using the machine, so it is off
// unless asked for. Off by default; there is no configuration file yet, so the
// switch is a TJS call for now.
bool PointerFocusEnabled();
void SetPointerFocusEnabled(bool enabled);

// The whole of an action that presses one named control on the current screen:
// check the state, check the control, press it, answer. `action` names it in
// the log and in the "does not accept arguments" refusal.
void HandleScreenButtonAction(std::string const &id,
                              std::string const &data,
                              char const *action,
                              char const *state,
                              char const *button,
                              char const *okMessage,
                              char const *failMessage);

// The language a reader was asked for: 0 is the Japanese original, -1 means
// whatever the game is displaying. Both readers take the same optional field.
int ParseReadLanguage(std::string const &data);

// Schemas whose bounds are only known at registration time, because they
// describe what is on screen at that moment.
std::string BoundedIntSchema(char const *field, int maximum);
// The same, when the range does not start at one: the slot screens number their
// ten cells by what is printed under them, so the page decides both ends.
std::string RangeIntSchema(char const *field, int minimum, int maximum);
std::string EnumStringSchema(char const *field,
                             std::vector<char const *> const &values);
