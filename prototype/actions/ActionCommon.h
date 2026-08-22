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
std::string EnumStringSchema(char const *field,
                             std::vector<char const *> const &values);
