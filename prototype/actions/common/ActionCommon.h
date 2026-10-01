#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

// Pieces the screen modules share.
//
// Each screen owns its own actions, but a few things are the same wherever they
// are used: pressing a control the screen carries, building a bounded schema,
// and the two waits the game imposes on us.

// Waits for the screen to catch up instead of sleeping through the wait.
//
// `ready` is asked every few milliseconds until it says yes, and `timeoutMs`
// is the old blind sleep kept as the worst case: a screen that never changes
// costs exactly what it used to, while the ordinary one costs what it actually
// takes. Returns what the last answer was, so a caller can tell "it happened"
// from "it ran out".
//
// Each ask crosses to the game's main thread, so the interval is not tiny: the
// point is to stop waiting on a screen that is already there, not to poll it
// as fast as possible.
bool WaitForScreen(std::function<bool()> const &ready, DWORD timeoutMs);

// How long the game takes to settle after presses it does not acknowledge.
// Measured against the save list: the page swap and the confirmation dialog
// both land late, and neither has a readable "ready" flag to poll instead.
constexpr DWORD kPageChangeSettleMs = 1200;
constexpr DWORD kDialogSettleMs = 150;
// How long a hover preview stays up before the press: the backlog's jump
// preview, the gallery's picture. A player sees it while moving the cursor;
// without the pause the next screen would open over a preview that never
// rendered.
constexpr DWORD kHoverPreviewMs = 400;

// The same gesture on an ordinary button, where there is no preview to render
// -- only the game's own highlight and hover sound, which start as soon as the
// handler runs. Long enough for the engine to get to them, short enough not to
// be a third of the action's time.
constexpr DWORD kHoverSoundMs = 250;

// How often WaitForScreen asks. One ask is a crossing to the game's main
// thread, about ten milliseconds of it, so asking much faster would spend
// more on the asking than on the waiting.
constexpr DWORD kScreenPollMs = 40;

// The refusal for a press that arrived while an earlier one is still on its way
// through the engine. One sentence everywhere, so it reads as one rule rather
// than as a different problem per screen.
constexpr char const *kPressStillRunning =
    "The last press is still going through; try again in a moment.";

// Answers an action that pressed something. A press refused because an earlier
// one has not left the engine's queue yet is told apart from a control that
// cannot be pressed at all: the two look the same to the caller, and only the
// first is worth trying again.
void AnswerPress(std::string const &id,
                 bool ok,
                 char const *okMessage,
                 char const *failMessage);

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

// A screen describing itself on the way in.
//
// The same description twice in a row says nothing the second time, and that
// happens for a reason worth keeping: a reconnect makes the plugin forget which
// screen it had synced, so every screen introduces itself again -- while the
// context it is repeating survived the disconnect on Neuro's side. So the last
// announcement is remembered and an identical one is dropped.
//
// Forgotten when the screen actually changes, not when the session restarts:
// leaving a screen and coming back is a new arrival and deserves saying again,
// even if the words match.
bool SendScreenAnnouncement(std::string const &text);
void ForgetScreenAnnouncement();

// Schemas whose bounds are only known at registration time, because they
// describe what is on screen at that moment.
std::string BoundedIntSchema(char const *field, int maximum);
// The same, when the range does not start at one: the slot screens number their
// ten cells by what is printed under them, so the page decides both ends.
std::string RangeIntSchema(char const *field, int minimum, int maximum);
std::string EnumStringSchema(char const *field,
                             std::vector<char const *> const &values);
