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
// The scenario the screen runs, from the active conductor. This is what the
// slot screens go by: their tabs change the scenario, while the state string
// does not follow and kag.currentStorage names the scene behind a menu opened
// in play.
bool ProbeScenarioIs(char const *expected, char const *tag);
// The screens that share the plain "back" control: option, load, save, voice
// and backlog. Only some of them also carry a "title" control, so
// return_to_title still stands on its own button probe.
bool ProbeStateIsMenuScreen(char const *tag);

// One poll of everything the capability refresh needs. Runs at 5 Hz on the game
// main thread, so it is deliberately a single round trip.
struct ScreenSnapshot {
	std::string state;
	// The scenario the screen is running, e.g. "load.ks", from the active
	// conductor. The save screen's tabs move between load.ks, save.ks and
	// voice.ks, and this is what follows them: `state` keeps reporting whichever
	// of the three was entered first. Not kag.currentStorage either -- a menu
	// opened during play runs on the extra conductor, and that field keeps
	// naming the scene behind it.
	std::string storage;
	bool dialogVisible = false;
	bool choiceVisible = false;
	bool quickMenuOut = false;
	bool textWindowHidden = false;
	bool autoMode = false;
	bool skipMode = false;
	// The title's own buttons. Visibility and usability are kept apart because
	// the game uses both: Continue and Extra are drawn greyed out until they
	// apply, while the True End is not drawn at all until it is unlocked.
	// `titleReady` is the Load button, the one entry always there, so it marks a
	// title that is built rather than one still building.
	bool titleReady = false;
	bool titleShowsContinue = false;
	bool titleCanContinue = false;
	bool titleShowsExtra = false;
	bool titleCanOpenExtra = false;
	bool titleShowsTrueEnd = false;
};
bool ProbeScreen(ScreenSnapshot &out);

// Controls on the current screen, kag.current.names[...]: "cont" on the title,
// "back" and "title" on the settings screens. Pressing the screen's own button
// rather than calling SystemAction directly keeps the action visible on screen.
bool ProbeScreenButtonReady(char const *button, char const *tag);
bool DispatchScreenButton(char const *button, char const *tag);
// Runs the button's own enter or leave handler, so the game reacts as it does
// to a cursor arriving or departing: the button lights up and shows whatever it
// reveals -- the title's Continue shows a preview of the save it would resume.
// Only visibility is required: a greyed button can still be pointed at, the
// same as with a mouse. `on` false is the departure, which has to be sent
// explicitly because no cursor ever moves away on its own.
bool DispatchScreenButtonHover(char const *button, bool on, char const *tag);
// Whether the cursor is resting on the button as far as the game is concerned.
bool ProbeScreenButtonHovered(char const *button, char const *tag);
// The chapter and date of the save the title's Continue would resume, empty
// when there is none. What the hover preview draws, in words.
bool ProbeContinueSave(std::string &chapter, std::string &date);

// The language screen the game opens once, on the very first launch. It reports
// itself as "option" like the settings screen, so only the controls tell them
// apart: it carries lang_text and no "title".
bool ProbeLanguageScreen(char const *tag);

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
// retracted, so _dropdown is the only usable position flag.
bool ProbeQuickMenuCanDropDown(char const *tag);
bool ProbeQuickMenuHasUpdown(char const *tag);
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
// slots' enabled flags cannot tell two pages apart when saves run
// consecutively. `page` comes back too -- 0 on the quick-save tab, 1..lastPage
// on load/save.
bool ProbeSaveWindow(int &offset, int &page);

// Which of the save screen's four tabs are on the tab bar, and which of those
// can be pressed, as the bit flags the router defines. The tab that is open has
// no button of its own, so it is absent from both masks.
bool ProbeSaveTabs(int &visibleMask, int &enabledMask);

// Whether the ten slots on screen are the ten bookmarks starting at `base`,
// checked by comparing each slot's enabled flag against whether that bookmark
// exists. There is no readable page indicator, and a page button press does not
// always land, so this is what stands between "asked for page 3" and actually
// being on it. Only meaningful once the page has settled.
bool ProbeSlotsShowBase(int base, char const *tag);

// The ten slots the screen is showing, as "<slot>	<what>	<when>" lines with
// `slot` numbered from 1 the way the screen numbers them, top left first. Empty
// slots are listed too, with both fields blank: their numbers are what
// save_game accepts. `base` and `page` come back as well -- page 0 is the quick
// tab. `voice` picks the fields, since the voice tab holds lines rather than
// saves.
bool ProbeSlotWindow(bool voice, int &base, int &page, std::string &out);

// How many page buttons the screen carries. Unlike the settings tabs, the page
// of the open window has a button too, so this is the page count as it stands.
int ProbeSavePageCount();

// The voice tab is the same paged list in a third mode: each entry is a
// recorded line, and pressing one replays it. Its rows carry more than the save
// rows do -- speaker, scene and the line itself -- through the store's own
// getItemInfo, which is why these do not go through the bookmark calls.
bool ProbeVoiceExists(int index, char const *tag);
// Which way the voice collection was opened: keeping the current line, or
// browsing what is already kept. The screen is the same either way, and the
// slots do opposite things -- vsave leaves the line in tf.VoiceSave, vload
// deletes it.
bool ProbeVoiceSaveMode(char const *tag);

// Which of the backlog's four scroll controls are usable right now, plus which
// of its two modes is showing, as the bit flags the router defines. They follow
// the reading position -- at the bottom of the log the two downward ones go
// dark -- so this is re-read while the screen is open rather than sampled once.
bool ProbeBacklogButtons(int &flags);
// Which of the five per-line slots on screen currently offer a jump and a voice
// replay, as bit masks over slots 0..4. The buttons live in the backlog's
// blockBaseLayer child and report it through `visible`, not `enabled`; the
// slots are window-relative, so the same number means a different line after
// scrolling.
bool ProbeBacklogLineButtons(int &jumpMask, int &voiceMask, int &saveMask);
// How many lines the conversation history holds, and a window of it as
// "<index>	<speaker>	<line>" rows with `index` numbered from 1. Both come from
// the player object the backlog reads, so neither depends on what is on screen.
// Speaker and line are localized: the displayed name is the "disp" key, not
// "name": the Japanese column carries no "disp" at all, so the raw name is
// used there instead.
bool ProbeHistoryCount(int &total);
// `langType` picks the language column: 0 is the Japanese original, -1 means
// whatever the game is currently displaying (kag.languageType).
bool ProbeHistoryWindow(int from, int count, int langType, std::string &out);
// The line on screen right now, as "<speaker>	<line>". Comes from the entry
// the monitor keeps from the render path, so it can be re-read in either
// language.
bool ProbeCurrentLine(int langType, std::string &out);

// Presses one, through the same Current.cmd the button itself carries.
// The jump rows also carry a hover pair, showJumpView/hideJumpView, which puts
// a preview of the target scene behind the log; passing "showJumpView" or
// "hideJumpView" as `verb` drives it the same way.
bool DispatchBacklogLineButton(char const *verb, int index, char const *tag);

// The Extra screen: a nine-square picture grid over four pages, with the music
// player's track list beside it. Every square is pressable -- there is no
// locked state to test -- so what matters is the page, how many tracks the list
// offers, which of its four arrows are live, and whether a picture is currently
// open over the grid. `variantsLeft` is how many more views that picture holds.
//
// The open-picture part comes from the gallery controller, which is only in
// hand after a square has been pressed once; before that it reports closed,
// which is true. `squareCount` is how many of the nine cells the current page
// actually uses: the last page carries fewer, and the unused ones are simply
// invisible.
bool ProbeGallery(int &page,
                  int &pageCount,
                  int &squareCount,
                  int &trackCount,
                  int &scrollMask,
                  bool &viewOpen,
                  int &variantsLeft);
// Presses a track in the music list. The picture squares go through the
// screen's own button dispatch instead: Current.cmd does nothing for them.
bool DispatchGalleryItem(char const *verb, int index, char const *tag);
// Leaves the open picture, through the right click the gallery scenario itself
// listens for (*view_rclick -> *view_quit). Not the controller's own onNext:
// that reaches into an object the engine has already released once a video is
// playing, and killed the game outright (cgmode.ks:27, 2026-08-20).
// Steps to the next view of the open picture, by clicking into the picture
// layer -- the scenario waits there and the window's own click never arrives.
bool DispatchGalleryNextView(char const *tag);
bool DispatchGalleryExit(char const *tag);
bool HoverGalleryItem(char const *prefix, int index, char const *tag);

// Asks to quit. Goes through SystemAction.exit(), which raises the AskExit
// confirmation, rather than the title button: that button's own expression
// is a bare kag.close() and would end the process with nothing asked.
bool DispatchExitRequest(char const *tag);

// How many tab buttons the current screen carries, counted by visibility.
// The settings screen has six; other menu screens have none.
int ProbeTabCount();

// Whether a modal confirmation is on screen right now.
bool ProbeDialogVisible(char const *tag);

// What the dialog on screen is asking, as the engine's own tag: "AskTitle",
// "AskExit", "AskJump", "AskLoad", and so on. The field keeps its last value
// after the dialog closes, so only read it while one is up.
bool ProbeDialogTextPart(std::string &part);

// The modal confirmation dialog; `confirm` picks the "yes" or "no" link.
bool ProbeDialogLinkReady(bool confirm, char const *tag);
bool DispatchDialogLink(bool confirm, char const *tag);
