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
	bool autoMode = false;
	bool skipMode = false;
	// The bar's own enabled flags for the three buttons whose availability the
	// game computes from state we would otherwise have to mirror: a current
	// voice to replay, a scene history to step back into, a next stop to jump
	// to. Reading the buttons keeps that logic in one place -- the game's.
	bool canReplayVoice = false;
	bool canGoPrevious = false;
	bool canGoNext = false;
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

// Saved games. Slots are numbered from 0 here, matching kag's bookmark index;
// the action numbers them from 1 the way the screen does. An empty slot reports
// enabled = 0 on its button, so ProbeScreenButtonReady already rejects it --
// this is for telling Neuro *why* rather than just refusing.
bool ProbeBookmarkExists(int number, char const *tag);
// Whether pressing a slot took: either the load confirmation is up, or the game
// went straight back to play because SystemConfig.askload is off.
bool ProbeLoadAccepted(char const *tag);
// The base bookmark index of the ten slots currently on screen, from the store
// instance the monitor catches (see context_monitor.tjs). This is the only
// reliable reading: the screen shows nothing about which page it is on, and the
// slots' enabled flags cannot tell two pages apart when saves run consecutively.
// `page` comes back too -- 0 on the quick-save tab, 1..lastPage on load/save.
bool ProbeSaveWindow(int &offset, int &page);

// Whether the ten slots on screen are the ten bookmarks starting at `base`,
// checked by comparing each slot's enabled flag against whether that bookmark
// exists. There is no readable page indicator, and a page button press does not
// always land, so this is what stands between "asked for page 3" and actually
// being on it. Only meaningful once the page has settled.
bool ProbeSlotsShowBase(int base, char const *tag);

// The saves themselves, as "<slot>	<scene>	<date>" lines, slot numbered from
// 1. Capped at `limit` entries; `total` receives how many exist in all. Neuro
// cannot see the screen, so without this the slot numbers mean nothing to her.
bool ProbeBookmarkList(int limit, std::string &out, int &total);
// The slot numbers, from 1, of the first `limit` free slots outside the quick
// range -- what save_game will accept. `total` receives how many are free.
bool ProbeFreeSlots(int limit, std::string &out, int &total);

// The voice tab is the same paged list in a third mode: each entry is a
// recorded line, and pressing one replays it. Its rows carry more than the save
// rows do -- speaker, scene and the line itself -- through the store's own
// getItemInfo, which is why these do not go through the bookmark calls.
bool ProbeVoiceExists(int index, char const *tag);
// "<slot>	<speaker>	<scene title>" lines, slot numbered from 1.
bool ProbeVoiceList(int limit, std::string &out, int &total);

// Which of the backlog's four scroll controls are usable right now, as the bit
// flags the router defines. They follow the reading position -- at the bottom of
// the log the two downward ones go dark -- so this is re-read while the screen
// is open rather than sampled once.
bool ProbeBacklogButtons(int &flags);

// Whether a modal confirmation is on screen right now.
bool ProbeDialogVisible(char const *tag);

// The modal confirmation dialog; `confirm` picks the "yes" or "no" link.
bool ProbeDialogLinkReady(bool confirm, char const *tag);
bool DispatchDialogLink(bool confirm, char const *tag);
