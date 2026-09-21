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
// Whether the settings screen is the one showing, its own sub-pages included.
// Narrower than the menu-screen test above, which counts the save list and the
// backlog as menus too.
bool ProbeStateIsSettingsScreen(char const *tag);

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
	// Whether the quick menu's layer is on screen at all. Apart from
	// `quickMenuOut` because the two can disagree: a scene entered through the
	// engine's own start point leaves the layer hidden, and the bar then reports
	// itself out with nothing for the player to see or reach.
	bool quickMenuVisible = false;
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
	// A panel layer is up over the screen. Read here rather than by a probe of
	// its own: the per-slot edit menu runs its own scenario, so while it stands
	// the storage names custom.ks and the screen underneath stops being
	// recognisable.
	bool panelShowing = false;
	// That panel is the edit menu and has finished building. The two are kept
	// apart because there is a window between them -- the panel exists before
	// its entries do -- and a set rebuilt inside that window describes neither
	// the screen nor the menu.
	bool editMenuReady = false;
	// A movie is on screen. While one plays nothing waits for a click, so the
	// text cannot be advanced and only skipping the movie applies.
	bool moviePlaying = false;
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
// The language screen the game opens once, on the very first launch. It reports
// itself as "option" like the settings screen, so only the controls tell them
// apart: it carries lang_text and no "title".
bool ProbeLanguageScreen(char const *tag);

// A suspend-save tagged "continue" exists.
bool ProbeCanContinue(char const *tag);

// The dialogue is waiting for a click, and the click itself.
bool ProbeClickWaiting(char const *tag);
bool DispatchPrimaryClick(char const *tag);

// Advancing the dialogue in one trip: the two conditions and the click travel
// together instead of as three calls.
//
// Each call crosses to the game's main thread and waits its turn there, about
// fourteen milliseconds apiece, and advancing is by far the most repeated
// action there is -- eleven thousand times in a single route. Splitting it
// three ways cost more than the work.
//
// The answer says why, not just whether. Refusing every case with the same
// sentence left Neuro to guess, and during a movie she would keep trying
// something that cannot work until it ends.
enum class AdvanceResult {
	Unavailable,  // the game was asked and did not answer usefully
	Advanced,
	NotInStory,  // a menu, the title, a screen that is not the dialogue
	MoviePlaying,
	NothingWaiting,  // the story is running but no click is due yet
	StillRunning,    // the press before it has not reached the engine yet
};
AdvanceResult AdvanceDialogue(char const *tag);

// The select layer is up with at least one enabled option; `index` is 0-based
// here, matching the engine array.
// Whether the last press was refused because the one before it is still on
// its way through the engine. Only meaningful right after a press that
// returned false, on the thread that made it.
// Drops the pending-press flags. For the boundary a press can be lost across:
// the screen changing between the request and the engine running it.
void ClearPendingPresses();

bool LastPressWasBusy();

// Ends a playing movie with an ordinary click.
bool DispatchMovieSkip(char const *tag);

bool ProbeChoiceLayerReady(char const *tag);
bool ProbeChoiceButtonReady(int index, char const *tag);
bool DispatchChoice(int index, char const *tag);

// The quick menu bar. updown(1)/updown(0) is the real slide; the panel's own
// open()/close() are empty stubs and showing/visible read 1 even while it is
// retracted, so _dropdown is the only usable position flag.
bool ProbeQuickMenuCanDropDown(char const *tag);
// Whether the bar actually arrived, asked after pressing for it. The game lets
// a hidden layer report itself opened, so this is the only reading that matches
// what the player sees.
bool ProbeQuickMenuCameOut(char const *tag);
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

// The per-slot Edit button, `position` counting from 0 over the ten slots on
// screen. These live in kag.current.buttons, a different collection from the
// kag.current.names every other screen control comes from, and their state is
// per slot: only slots holding a save carry a usable one.
bool ProbeEditButtonReady(int position, char const *tag);
bool DispatchEditButton(int position, char const *tag);

// The menu that Edit opens. It is neither a dialog nor part of the screen: the
// game runs a scenario that raises a panel layer over it, so kag.panelShowing
// is what says it is up, and the conductor sleeps until it closes. The four
// entries come back as bits in the order the router names them.
//
// Both readings are wanted, not just the usable one: the game draws an entry it
// will not accept rather than hiding it, and "greyed out for this slot" is a
// different thing to say than "not there at all".
bool ProbeEditMenuItems(int &visibleMask, int &enabledMask, char const *tag);
bool DispatchEditMenuItem(char const *item, char const *tag);
// The cursor arriving at an entry, without pressing it. Wanted for the comment
// entry, which is chosen but never pressed: without this it is the only choice
// on any screen that happens with nothing lighting up.
bool DispatchEditMenuItemHover(char const *item, bool on, char const *tag);
// Closing without choosing. The game's own gesture is a click outside the
// panel, which its mouse handler turns into exactly this call.
bool DispatchCloseEditMenu(char const *tag);

// The slot the open edit menu belongs to, 0-based over the ten on screen. The
// game puts it in a temporary variable when the menu opens, and that is the
// only place it is written down.
bool ProbeEditMenuSlot(int &position, char const *tag);

// True while the game's own comment window is up. It is a native Win32 dialog:
// TJS calls still get through it, but the engine is inside that window's own
// message loop, so nothing of the game's own runs until it closes.
bool ProbeCommentWindowOpen(char const *tag);

// The comment a save carries now, empty when it has none. Wanted because the
// game's own window opens with this text already in it: editing is what a
// player does there, and without it the only thing Neuro can do is replace.
bool ProbeSlotComment(int position, std::string &out, char const *tag);

// Writes the comment the game keeps beside a save, and redraws the slot. This
// is what the game itself does once its comment window returns a string; we do
// it without the window, which cannot be typed into from here.
bool DispatchSetComment(int position, std::string const &text, char const *tag);

// After Copy or Move the screen enters a mode: the next slot pressed is the
// destination, and until one is, every Edit button is gone. Comes back as 0 for
// no mode, or the router's copy/move/swap values. The mode belongs to the
// store, not to the tab, so it survives switching tabs and pages -- the
// destination may be anywhere.
bool ProbeCopyMode(int &mode, char const *tag);

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
bool ProbeSlotWindow(bool voice,
                     int &base,
                     int &page,
                     int &fresh,
                     std::string &out);
// Everything one slot holds, as "<exists>	<title>	<date>	<comment>	<last
// line>".
//
// The slot list carries two fields per row because ten rows of everything would
// bury the screen; this is the rest, for one slot at a time. The comment is not
// part of the record and comes from getCommentText, the same call the edit menu
// writes through -- it is what the player types over the last line. `newest` is
// the "New" badge the screen puts on the slot written last, which lives on the
// store rather than on the record.
bool ProbeSlotDetails(int position, std::string &out);

// How many page buttons the screen carries. Unlike the settings tabs, the page
// of the open window has a button too, so this is the page count as it stands.
int ProbeSavePageCount();

// The voice tab is the same paged list in a third mode: each entry is a
// recorded line, and pressing one replays it. Its rows carry more than the save
// rows do -- speaker, scene and the line itself -- through the store's own
// getItemInfo, which is why these do not go through the bookmark calls.
bool ProbeVoiceExists(int index, char const *tag);
// One kept line, as "<speaker>	<scene>	<text>	<newest>". The text is the one
// the record holds and cannot be asked for in another language: it was frozen
// in whatever language the game was in when it was kept.
bool ProbeVoiceLine(int index, std::string &out);
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
// Grouped rather than passed as three loose int&: three masks in a row is the
// shape where two get swapped, and a swap here reads one button's state as
// another's.
struct BacklogLineButtons {
	int jumpMask = 0;
	int voiceMask = 0;
	int saveMask = 0;
};
bool ProbeBacklogLineButtons(BacklogLineButtons &buttons);
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
// The same window while the backlog is showing scenes instead of lines, as
// "<index>	<scene>	<title>" rows. The player object is the same one; what it
// holds changes with the mode, which is why the count needs no separate probe.
//
// The title is the scenario's own name for the scene -- "Eden, Part 1" -- and
// it is the only place the game puts that name into words. It arrives as one
// entry per language, indexed the way the game indexes it.
bool ProbeSceneWindow(int from, int count, int langType, std::string &out);
// Which part of the history the backlog is showing at this moment: the index of
// the topmost row and how many rows are up. The two numberings the backlog uses
// -- the row on screen, which the per-line actions take, and the index into the
// history, which reading takes -- only meet here.
bool ProbeBacklogWindowRange(int &first, int &count);
// How many entries the backlog is listing right now. Not the same as the
// history: in the scene list one entry stands for a whole scene, so this is
// what "out of" has to be counted against.
bool ProbeBacklogBlockCount(int &count);

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
struct GalleryState {
	int page = 0;
	int pageCount = 0;
	int squareCount = 0;
	int trackCount = 0;
	// Where the visible rows start in the whole list. Track numbers are absolute,
	// so this is what ties one to the button showing it.
	int trackOffset = 0;
	int scrollMask = 0;
	int variantsLeft = 0;
	bool viewOpen = false;
};
bool ProbeGallery(GalleryState &state);
// The open picture's scrolling. Only the first record opens at full size, and
// only on its second view: the picture is larger than the frame and the player
// moves it with the wheel, the arrow keys or by dragging. `canScroll` carries
// the axes the game will actually move -- bit 0 horizontal, bit 1 vertical --
// and comes back 0 for every view that fits, which is most of them.
struct CgScrollState {
	int canScroll = 0;
	int left = 0;
	int maxLeft = 0;
	int top = 0;
	int maxTop = 0;
};
bool ProbeCgScroll(CgScrollState &state, char const *tag);
// Moves it by whole steps, the way the arrow keys do: the game multiplies each
// by its own step and clamps to the edges, so a step past the end simply stops
// there.
bool DispatchCgScroll(int horizontal, int vertical, char const *tag);

// The music player's volume slider on the Extra screen, 0 to 100. Three things
// move together when a player drags it, and all three are read back here: the
// stored setting, the knob on screen, and the value the sound actually plays
// at.
bool ProbeMusicVolume(int &volume, char const *tag);
// Sets it the way the screen's own handler does -- store, apply, move the knob.
// Setting only the knob moves the picture of the volume without changing the
// volume, which is why all three are written rather than the obvious one.
bool DispatchMusicVolume(int volume, char const *tag);

// The scroll bar beside the music list, in the game's own pixels: where it
// stands now, and how far down it goes. The list is longer than its frame, so
// the entries below the visible rows are reachable only through this.
bool ProbeMusicScroll(int &position, int &limit, char const *tag);
// Moves it the way a drag on the bar does. The game snaps the result to whole
// rows and animates the move over about 200 ms, so a read-back has to wait or
// it returns where the bar started.
bool DispatchMusicScroll(int position, char const *tag);

// The rows the music list is showing, one per line, as track number, sound file
// name and title separated by tabs, with a trailing 1 on the one playing. The
// number is the track's place in the whole list, not its place on screen, so it
// means the same track however far the list has been scrolled.
//
// The first line is the total and the window's offset, tab separated: the total
// for saying how long the list is, the offset for turning a number back into
// the button that carries it.
//
// The titles are the Japanese ones the game stores; the English words on the
// buttons are pictures out of the interface atlas, keyed by file name, and
// exist as text nowhere in the data.
bool ProbeMusicList(std::string &rows, char const *tag);

// Which track the player is on: its sound file name and the title the game
// keeps for it. Empty when nothing is playing.
bool ProbeMusicPlaying(std::string &storage,
                       std::string &title,
                       char const *tag);

// Presses a track in the music list. The picture squares go through the
// screen's own button dispatch instead: Current.cmd does nothing for them.
bool DispatchGalleryItem(char const *verb, int index, char const *tag);
// Leaves the open picture, through the right click the gallery scenario itself
// listens for (*view_rclick -> *view_quit). Not the controller's own onNext:
// that reaches into an object the engine has already released once a video is
// playing, and kills the game outright.
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

// The scene title of the save the load screen is asking about, read while its
// confirmation is up: the screen remembers the slot it asked for, and closes
// with the answer. Empty when there is no such save to name.
bool ProbeLoadSelectionTitle(std::string &title);

// The same for the quick save, which has no slot screen. Its bookmark number is
// asked of the game, which moves it away from the engine's default.
bool ProbeQuickLoadTitle(std::string &title);

// The modal confirmation dialog; `confirm` picks the "yes" or "no" link.
bool ProbeDialogLinkReady(bool confirm, char const *tag);
bool DispatchDialogLink(bool confirm, char const *tag);
