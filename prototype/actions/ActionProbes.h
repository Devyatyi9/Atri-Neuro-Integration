#pragma once

#include <string>

// Every TJS expression the action layer runs, named.
//
// The handlers follow "query facts in TJS, decide in C++": each step asks the
// engine one yes/no question and the C++ side sequences them. Keeping the
// expressions here rather than inline in the handlers means the engine-specific
// knowledge -- which object holds which control, which flag is the real one --
// lives in one file, and the handlers read as the sequence of checks they are.
//
// All of these return true only when the probe both ran and its condition
// holds; a failed probe is logged with `tag` and reported as false, because
// every call site treats "could not ask" and "answer is no" the same way.
// `tag` names the call site in the log, e.g. "continue button".

// Screen state, as reported by SystemHook.currentState.
bool ProbeStateIs(char const *expected, char const *tag);
// The screens that share the plain "back" control: option, load, save, voice
// and backlog. Only some of them also carry a "title" control, so
// return_to_title still stands on its own button probe.
bool ProbeStateIsMenuScreen(char const *tag);

// One poll of everything the capability refresh needs. Runs at 5 Hz on the game
// main thread, so it is deliberately a single round trip.
struct ScreenSnapshot {
	std::string state;
	bool dialogVisible = false;
	bool choiceVisible = false;
	bool quickMenuOut = false;
	bool textWindowHidden = false;
};
bool ProbeScreen(ScreenSnapshot &out);

// Controls on the current screen, kag.current.names[...]: "cont" on the title,
// "back" and "title" on the settings screens. Pressing the screen's own button
// rather than calling SystemAction directly keeps the action visible on screen.
bool ProbeScreenButtonReady(char const *button, char const *tag);
bool DispatchScreenButton(char const *button, char const *tag);

// A suspend-save tagged "continue" exists.
bool ProbeCanContinue(char const *tag);

// The dialogue is waiting for a click, and the click itself.
bool ProbeClickWaiting(char const *tag);
bool DispatchPrimaryClick(char const *tag);

// The select layer is up with at least one enabled option; `index` is 0-based
// here, matching the engine array.
bool ProbeChoiceLayerReady(char const *tag);
bool ProbeChoiceButtonReady(int index, char const *tag);
bool DispatchChoice(int index, char const *tag);

// The quick menu bar. updown(1)/updown(0) is the real slide; the panel's own
// open()/close() are empty stubs and showing/visible read 1 even while it is
// retracted, so _dropdown is the only usable position flag (menu-navigation.md).
bool ProbeQuickMenuCanDropDown(char const *tag);
bool ProbeQuickMenuHasUpdown(char const *tag);
bool ProbeQuickMenuIsOut(char const *tag);
// Both report whether the bar actually ended up in the requested position.
// Retracting is not symmetric with opening: updown(0) only takes effect while
// canDropDown holds, and the bar re-drops on its own whenever the cursor is
// over it (_dropdown = locked || inMouseCursor).
bool DispatchQuickMenuOpen(char const *tag);
bool DispatchQuickMenuClose(char const *tag);
// Buttons on the bar itself, kag.QuickMenu.names[...]: "option" and the ones
// still unused (auto, hide, log, save, load, skip, ...).
bool ProbeQuickMenuButtonReady(char const *button, char const *tag);
bool DispatchQuickMenuButton(char const *button, char const *tag);

// The text window hidden away by the bar's "hide" button (or Escape). Nothing
// is on screen in that state -- the hide button itself reports enabled = 0 --
// so bringing it back goes through the engine call the click would reach, which
// is the one affordance a player has left.
bool DispatchShowTextWindow(char const *tag);

// The modal confirmation dialog; `confirm` picks the "yes" or "no" link.
bool ProbeDialogLinkReady(bool confirm, char const *tag);
bool DispatchDialogLink(bool confirm, char const *tag);
