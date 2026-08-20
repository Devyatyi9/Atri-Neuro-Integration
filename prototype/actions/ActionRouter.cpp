#include "ActionRouter.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "Logging.h"
#include "NeuroClient.h"

namespace {

std::atomic<bool> g_ContinueActionInFlight(false);
std::atomic<ULONGLONG> g_ContinueActionStartedAt(0);
std::mutex g_ActionIdsMutex;
std::deque<std::string> g_ActionIds;
std::unordered_set<std::string> g_ActionIdSet;
constexpr size_t kMaxRememberedActionIds = 256;
// Waits for screens that redraw a frame or two after the control is pressed,
// measured against the load screen: the page swap and the confirmation dialog
// both land late. Neither has a readable "ready" flag to poll instead.
constexpr DWORD kPageChangeSettleMs = 1200;
constexpr DWORD kDialogSettleMs = 150;
// How long the jump preview stays up before the row is actually pressed. The
// row carries a hover pair that puts the target scene behind the log, and a
// player sees it while moving the cursor; without the pause the confirmation
// would open over a preview that never rendered.
constexpr DWORD kHoverPreviewMs = 400;

std::atomic<int> g_SyncedCapabilityState(-1);
// Whether the "still starting up" notice reached the server. It cannot simply
// be sent once on entering the loading state, because that happens before the
// SDK connects.
std::atomic<bool> g_TitleLoadingNoticeSent(false);
constexpr int kCapabilityStateUnknown = -1;
constexpr int kCapabilityStateTitle = 0;
constexpr int kCapabilityStateMenu = 2;
// Startup and the title screen are distinct capability states. The plugin
// initialises long before the title exists -- splashes run for roughly 24s --
// so registering continue_game at init advertised an action that was certain to
// fail for that whole window, and an autonomous engine does call it (observed
// in Gary: randy dispatched continue_game on its own). This state carries no
// actions and explains the wait through context instead.
constexpr int kCapabilityStateTitleLoading = 3;
// A confirmation dialog is modal: nothing else can succeed while it is up, so
// it gets its own state carrying only the answer action.
constexpr int kCapabilityStateDialog = 4;
// Play is not one state but a screen carrying four independent booleans: a
// choice is up, the quick menu bar is out, auto mode runs, skipping runs.
// Enumerating the combinations meant a constant per pair and would now mean
// sixteen, so play is encoded as a base plus flags. The value only has to be
// distinct per action set, which is all the dedup in SyncCapabilities needs.
//
// Choice vs text is a disposable action window in the sense of the SDK's own
// guidance (neuro-sdk/API/README.md, "Unregister disposable actions before
// sending result"): while the select layer is up, advancing the text does
// nothing, and select_choice stops being answerable the moment the layer
// closes. The bar and the two modes are toggles -- only one direction of each
// can apply at a time, and the bar's own buttons only exist while it is out.
constexpr int kCapabilityStatePlayBase = 0x100;
constexpr int kPlayFlagChoice = 1 << 0;
constexpr int kPlayFlagPanelOut = 1 << 1;
constexpr int kPlayFlagAuto = 1 << 2;
constexpr int kPlayFlagSkip = 1 << 3;
constexpr int kPlayFlagVoice = 1 << 4;
constexpr int kPlayFlagPrevious = 1 << 5;
constexpr int kPlayFlagNext = 1 << 6;
// The text window hidden away by the bar's hide button: the artwork is all
// that is left on screen, the hide button itself goes enabled = 0, and a click
// brings the window back instead of advancing. Only one action can apply.
constexpr int kCapabilityStateTextHidden = 8;
// The title screen with no suspend-save to continue from. Loading a save is
// still possible there, so it cannot be folded into title-loading.
constexpr int kCapabilityStateTitleNoSave = 9;
// Any screen that is not the title and not play. Which actions apply is
// decided by the controls the screen actually carries rather than by a list of
// state names: the backlog has a "back" control but no "title" one, and states
// nobody has visited yet (cgmode, scenemode, soundmode, search) would
// otherwise fall through every branch and leave the previous set registered.
constexpr int kCapabilityStateMenuBackOnly = 10;
// A recognised screen carrying none of the controls we drive. Registering
// nothing is the honest answer; the alternative was stale actions.
constexpr int kCapabilityStateBare = 11;
// The load screen. It carries the same back/title controls as the other menu
// screens, but also the save slots, and entering it is the moment to say what
// is in them -- the slot numbers mean nothing on their own.
constexpr int kCapabilityStateLoad = 12;
// The save screen. Same list as load, different mode: occupied slots are
// writable there rather than loadable, so what is worth saying on entry is
// which slots are free.
constexpr int kCapabilityStateSave = 13;
// The voice tab: the same list again, holding recorded lines instead of saves.
// Pressing one replays it, the way the backlog replays a line.
constexpr int kCapabilityStateVoice = 14;
// The backlog. Its four scroll controls come and go with the reading position,
// so like play it is a base plus flags rather than one constant per case. The
// base sits above the play range and the play test is bounded by it: without
// that, a backlog value passed the play check and registered play actions with
// the scroll flags read as choice/auto (observed 2026-08-20).
constexpr int kCapabilityStateBacklogBase = 0x200;
constexpr int kBacklogFlagPageUp = 1 << 0;
constexpr int kBacklogFlagPageDown = 1 << 1;
constexpr int kBacklogFlagTop = 1 << 2;
constexpr int kBacklogFlagEnd = 1 << 3;
// Which of the two lists is showing. The screen has one toggle for both
// directions, so only the one that would change something is offered.
constexpr int kBacklogFlagSceneList = 1 << 4;
// The five per-line slots, as two masks packed above the screen flags. They
// are part of the capability value so that scrolling -- which changes which
// lines are on screen -- re-registers and re-describes the numbering.
constexpr int kBacklogJumpShift = 5;
constexpr int kBacklogVoiceShift = 10;
constexpr int kBacklogSlotMask = 0x1f;
// How many saves the entering context lists before it just gives the total.
// Fifteen is one screenful, which keeps the message about as long as what a
// player sees at a glance.
constexpr int kListedSaves = 15;

bool ClaimActionId(std::string const &id) {
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	if (!g_ActionIdSet.insert(id).second) {
		return false;
	}
	g_ActionIds.push_back(id);
	if (g_ActionIds.size() > kMaxRememberedActionIds) {
		g_ActionIdSet.erase(g_ActionIds.front());
		g_ActionIds.pop_front();
	}
	return true;
}

// Result messages are what Neuro reads back, so each handler says which step
// refused rather than only that the action failed.
void HandleContinueAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "continue_game does not accept arguments.");
		return;
	}
	if (g_ContinueActionInFlight.exchange(true, std::memory_order_acq_rel)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Continue is already being started.");
		return;
	}
	g_ContinueActionStartedAt.store(GetTickCount64(), std::memory_order_release);

	// Title screen, a suspend-save tagged "continue", and the button itself
	// visible and dispatchable.
	bool const ok = ProbeStateIs("title", "continue state") &&
	                ProbeCanContinue("continue canContinue") &&
	                ProbeScreenButtonReady("cont", "continue button") &&
	                DispatchScreenButton("cont", "continue dispatch");
	if (!ok) {
		g_ContinueActionInFlight.store(false, std::memory_order_release);
		NeuroSendActionResult(id.c_str(), false, "Continue is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Continue started.");
}

void HandleAdvanceTextAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "advance_text does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "advance state") &&
	                ProbeClickWaiting("advance click") &&
	                DispatchPrimaryClick("advance dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, "Text advance is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Text advanced.");
}

void HandleSelectChoiceAction(std::string const &id, std::string const &data) {
	// Numbered from 1, matching the choice list the context message shows
	// ("1) ... 2) ..."). The engine array is 0-based, so the two disagreed:
	// reading "1)" off the context and sending 1 picked the second option.
	int number = -1;
	if (!ParseJsonInteger(data, "\"index\"", number) || number < 1) {
		NeuroSendActionResult(id.c_str(), false,
		                      "select_choice requires an integer \"index\" of 1 "
		                      "or more.");
		return;
	}
	int const index = number - 1;

	bool const ok = ProbeStateIs("game", "select state") &&
	                ProbeChoiceLayerReady("select layer") &&
	                ProbeChoiceButtonReady(index, "select button") &&
	                DispatchChoice(index, "select dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, "Choice is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Choice selected.");
}

void HandleCloseQuickMenuAction(std::string const &id,
                                std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "close_quick_menu does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "close_quick_menu state") &&
	                ProbeQuickMenuHasUpdown("close_quick_menu dispatchable") &&
	                DispatchQuickMenuClose("close_quick_menu dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Closing the quick menu is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Closed the quick menu.");
}

void HandleOpenQuickMenuAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "open_quick_menu does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "quick_menu state") &&
	                ProbeQuickMenuCanDropDown("quick_menu dispatchable") &&
	                DispatchQuickMenuOpen("quick_menu dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the quick menu is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the quick menu.");
}

void HandleOpenMenuAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "open_menu does not accept arguments.");
		return;
	}

	// The bar has to be out before its Option button is pressed: the button
	// reports visible/enabled even while retracted, so without that check the
	// settings screen could open with nothing on screen having been pressed.
	// Pressing the bar's own control rather than calling SystemAction.option()
	// is what keeps the action visible; same destination either way.
	bool const ok = ProbeStateIs("game", "open_menu state") &&
	                ProbeQuickMenuIsOut("open_menu dropdown") &&
	                ProbeQuickMenuButtonReady("option", "open_menu button") &&
	                DispatchQuickMenuButton("option", "open_menu dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the settings menu is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the settings menu.");
}

void HandleReturnToTitleAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "return_to_title does not accept arguments.");
		return;
	}

	// Offered on the settings screen, not during play. Calling SystemAction
	// .title() straight from game made the confirmation dialog appear out of
	// nowhere -- mid-choice, with nothing on screen explaining it. The settings
	// screen is where a player finds this button, so the route is
	// open_quick_menu -> open_menu -> return_to_title and every step is visible.
	//
	// The control's handler is SystemAction.title() -> _title(), which guards on
	// kag.startAnchorEnabled and raises the AskTitle dialog; see
	bool const ok = ProbeStateIsMenuScreen("return_to_title state") &&
	                ProbeScreenButtonReady("title", "return_to_title button") &&
	                DispatchScreenButton("title", "return_to_title dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Returning to the title screen is unavailable.");
		return;
	}
	// The game asks for confirmation when SystemConfig.askTitle is set, so this
	// action deliberately stops at the question rather than answering it: losing
	// unsaved progress should not happen from one call. answer_dialog becomes
	// available as soon as the dialog is up.
	NeuroSendActionResult(
	    id.c_str(), true,
	    "Asked to return to the title screen; confirm or cancel the dialog.");
}

void HandleAnswerDialogAction(std::string const &id, std::string const &data) {
	int confirmValue = 0;
	if (!ParseJsonBoolean(data, "\"confirm\"", confirmValue)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "answer_dialog requires a boolean \"confirm\".");
		return;
	}
	bool const confirm = confirmValue != 0;

	bool const ok = ProbeDialogLinkReady(confirm, "answer_dialog ready") &&
	                DispatchDialogLink(confirm, "answer_dialog dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, "No dialog is waiting.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      confirm ? "Confirmed the dialog."
	                              : "Cancelled the dialog.");
}

void HandleHideTextWindowAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "hide_text_window does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "hide_text_window state") &&
	                ProbeQuickMenuIsOut("hide_text_window dropdown") &&
	                ProbeQuickMenuButtonReady("hide", "hide_text_window button") &&
	                DispatchQuickMenuButton("hide", "hide_text_window dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Hiding the text window is unavailable.");
		return;
	}
	NeuroSendActionResult(
	    id.c_str(), true,
	    "Hid the text window. Nothing is on screen now except the artwork; "
	    "show it again to keep reading.");
}

void HandleShowTextWindowAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "show_text_window does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "show_text_window state") &&
	                DispatchShowTextWindow("show_text_window dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The text window is not hidden.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Brought the text window back.");
}

void HandleOpenLoadAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "open_load does not accept arguments.");
		return;
	}

	// Two routes to the same screen, each through the control the player would
	// use: the title screen carries its own Load button, and during play it is
	// the one on the quick menu bar. Which one applies is decided by the state,
	// not by trying both.
	bool ok = false;
	if (ProbeStateIs("title", "open_load state")) {
		ok = ProbeScreenButtonReady("load", "open_load title button") &&
		     DispatchScreenButton("load", "open_load title dispatch");
	} else if (ProbeStateIs("game", "open_load state")) {
		ok = ProbeQuickMenuIsOut("open_load dropdown") &&
		     ProbeQuickMenuButtonReady("load", "open_load bar button") &&
		     DispatchQuickMenuButton("load", "open_load bar dispatch");
	}
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the load screen is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the load screen.");
}

// auto and skip are the same bar button pressed twice: one press turns the
// mode on, the next turns it off, and the button stays enabled throughout.
// Which direction is registered comes from the mode flag, so the handler only
// has to press.
void HandleQuickMenuToggle(std::string const &id,
                           std::string const &data,
                           char const *action,
                           char const *button,
                           char const *okMessage,
                           char const *failMessage) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " does not accept arguments.").c_str());
		return;
	}

	std::string const tag(action);
	bool const ok = ProbeStateIs("game", (tag + " state").c_str()) &&
	                ProbeQuickMenuIsOut((tag + " dropdown").c_str()) &&
	                ProbeQuickMenuButtonReady(button, (tag + " button").c_str()) &&
	                DispatchQuickMenuButton(button, (tag + " dispatch").c_str());
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, failMessage);
		return;
	}
	NeuroSendActionResult(id.c_str(), true, okMessage);
}

// Stepping between scene breaks asks first (asknextscn / askprevscn), the way
// returning to the title does -- so the result says the question was raised,
// not that the jump happened. The confirmation is configurable, so whether it
// actually appeared is read back rather than assumed.
void HandleSceneStepAction(std::string const &id,
                           std::string const &data,
                           char const *action,
                           char const *button,
                           char const *askedMessage,
                           char const *doneMessage,
                           char const *failMessage) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " does not accept arguments.").c_str());
		return;
	}

	std::string const tag(action);
	bool const ok = ProbeStateIs("game", (tag + " state").c_str()) &&
	                ProbeQuickMenuIsOut((tag + " dropdown").c_str()) &&
	                ProbeQuickMenuButtonReady(button, (tag + " button").c_str()) &&
	                DispatchQuickMenuButton(button, (tag + " dispatch").c_str());
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, failMessage);
		return;
	}
	Sleep(kDialogSettleMs);
	NeuroSendActionResult(id.c_str(), true,
	                      ProbeDialogVisible((tag + " asked").c_str())
	                          ? askedMessage
	                          : doneMessage);
}

// A plain press of a control on the current screen, for the ones that need no
// argument and raise nothing: state, button, press.
void HandleScreenButtonAction(std::string const &id,
                              std::string const &data,
                              char const *action,
                              char const *state,
                              char const *button,
                              char const *okMessage,
                              char const *failMessage) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " does not accept arguments.").c_str());
		return;
	}
	std::string const tag(action);
	bool const ok = ProbeStateIs(state, (tag + " state").c_str()) &&
	                ProbeScreenButtonReady(button, (tag + " button").c_str()) &&
	                DispatchScreenButton(button, (tag + " dispatch").c_str());
	NeuroSendActionResult(id.c_str(), ok, ok ? okMessage : failMessage);
}

// The two per-line controls. Their numbering is over the lines currently on
// screen, so the slot is re-checked against what is visible at this moment.
void HandleBacklogLineAction(std::string const &id,
                             std::string const &data,
                             char const *action,
                             char const *verb,
                             bool wantJump,
                             char const *okMessage) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"line\"", number) || number < 1 || number > 5) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " requires a \"line\" between 1 and 5.").c_str());
		return;
	}
	int const slot = number - 1;

	std::string const tag(action);
	if (!ProbeStateIs("backlog", (tag + " state").c_str())) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works while the backlog is open.");
		return;
	}
	int jumpMask = 0;
	int voiceMask = 0;
	if (!ProbeBacklogLineButtons(jumpMask, voiceMask)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Cannot tell which lines are on screen right now.");
		return;
	}
	int const mask = wantJump ? jumpMask : voiceMask;
	if ((mask & (1 << slot)) == 0) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    wantJump ? "That line cannot be jumped to."
		             : "That line has no voice to replay.");
		return;
	}
	// Hover first, the way the row is reached with a cursor: the preview is the
	// only thing on screen that says which line this is, and the confirmation
	// that follows does not name it.
	if (wantJump) {
		DispatchBacklogLineButton("showJumpView", slot, (tag + " hover").c_str());
		Sleep(kHoverPreviewMs);
	}
	if (!DispatchBacklogLineButton(verb, slot, (tag + " dispatch").c_str())) {
		// Leaving a preview up over a press that never happened would be a lie
		// about what is selected.
		if (wantJump) {
			DispatchBacklogLineButton("hideJumpView", slot,
			                          (tag + " unhover").c_str());
		}
		NeuroSendActionResult(id.c_str(), false, "That line did not respond.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, okMessage);
}

// Reads a window of the conversation history. Registered only while the
// backlog is open: the history is readable at any time, but advertising it
// everywhere would put an action in front of Neuro on every screen for
// something she asked for by opening the log.
constexpr int kHistoryWindow = 4;

// Both readers take the same optional language: absent means whatever the
// game is showing, "japanese" means the original column. getLangInfo takes the
// language as its third argument, so no state has to be changed to read it.
int ParseReadLanguage(std::string const &data) {
	return data.find("japanese") == std::string::npos ? -1 : 0;
}

void HandleReadCurrentLineAction(std::string const &id,
                                 std::string const &data) {
	std::string line;
	if (!ProbeCurrentLine(ParseReadLanguage(data), line)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "There is no line on screen to read.");
		return;
	}
	size_t const tab = line.find('	');
	std::string message = "Current line: ";
	if (tab != std::string::npos && tab > 0) {
		message += line.substr(0, tab) + ": ";
	}
	message += tab == std::string::npos ? line : line.substr(tab + 1);
	NeuroSendActionResult(id.c_str(), true, message.c_str());
}

void HandleReadBacklogAction(std::string const &id, std::string const &data) {
	if (!ProbeStateIs("backlog", "read_backlog state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works while the backlog is open.");
		return;
	}
	int total = 0;
	if (!ProbeHistoryCount(total) || total <= 0) {
		NeuroSendActionResult(id.c_str(), false, "The history is not readable.");
		return;
	}

	// Without "from", the newest lines -- which is what "what was just said"
	// means. Numbered from 1 like everything else Neuro is given.
	int number = 0;
	bool const hasFrom = ParseJsonInteger(data, "\"from\"", number);
	if (hasFrom && (number < 1 || number > total)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    ("\"from\" must be between 1 and " + std::to_string(total) + ".")
		        .c_str());
		return;
	}
	int const first = hasFrom ? number - 1 : total - kHistoryWindow;

	std::string window;
	if (!ProbeHistoryWindow(first, kHistoryWindow, ParseReadLanguage(data),
	                        window) ||
	    window.empty()) {
		NeuroSendActionResult(id.c_str(), false, "Could not read those lines.");
		return;
	}

	std::string message = "Lines out of " + std::to_string(total) + ":";
	size_t start = 0;
	while (start < window.size()) {
		size_t const lineEnd = window.find('\n', start);
		std::string const row = window.substr(
		    start,
		    lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
		start = lineEnd == std::string::npos ? window.size() : lineEnd + 1;
		size_t const firstTab = row.find('	');
		size_t const secondTab = firstTab == std::string::npos
		                             ? std::string::npos
		                             : row.find('	', firstTab + 1);
		size_t const thirdTab = secondTab == std::string::npos
		                            ? std::string::npos
		                            : row.find('	', secondTab + 1);
		if (thirdTab == std::string::npos) {
			continue;
		}
		std::string const speaker =
		    row.substr(firstTab + 1, secondTab - firstTab - 1);
		bool const hasVoice = row[secondTab + 1] == '1';
		message += " " + row.substr(0, firstTab) + ") ";
		if (!speaker.empty()) {
			message += speaker + ": ";
		}
		message += row.substr(thirdTab + 1);
		// Marks the lines jump_to_line and replay_backlog_voice can act on once
		// they are scrolled into view.
		if (hasVoice) {
			message += " [voiced]";
		}
		message += ";";
	}
	NeuroSendActionResult(id.c_str(), true, message.c_str());
}

void HandleOpenBacklogAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "open_backlog does not accept arguments.");
		return;
	}

	// The screen opens a frame or two later: the click returns while the state
	// is still "game", and the poll picks up "backlog" on a later tick. Nothing
	// here waits for it -- the result says what was pressed, not what arrived.
	bool const ok = ProbeStateIs("game", "open_backlog state") &&
	                ProbeQuickMenuIsOut("open_backlog dropdown") &&
	                ProbeQuickMenuButtonReady("log", "open_backlog button") &&
	                DispatchQuickMenuButton("log", "open_backlog dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the backlog is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      "Opened the backlog of earlier lines.");
}

// Bookmarks are one flat range, and which tab shows a given one follows from
// the index: 0..9 are the quick saves, on a tab with a single page, and the rest
// live on load/save at ten to a page. Page N shows offset N * 10, so page 1
// starts at bookmark 10, not 0.
constexpr int kQuickSaveCount = 10;
constexpr int kVisibleSlots = 10;
constexpr int kSlotColumns = 5;

// Brings the shared save/load list to the window holding `bookmark` and returns
// its position 0..9 there, or -1 after sending the refusal itself.
//
// Load and save are the same screen in two modes, so this serves both.
int MoveToBookmark(std::string const &id,
                   int bookmark,
                   char const *firstTenMessage) {
	int const wantedOffset =
	    bookmark < kQuickSaveCount ? 0
	                               : bookmark / kVisibleSlots * kVisibleSlots;

	int offset = -1;
	int page = -1;
	if (!ProbeSaveWindow(offset, page)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "Cannot tell which slots the screen is showing right now.");
		return -1;
	}

	if (offset != wantedOffset) {
		// Switching tabs is left to the player: quick and normal saves are
		// different screens to look at, and moving between them behind her back
		// would make the slot numbers mean something else mid-action.
		if ((bookmark < kQuickSaveCount) != (page == 0)) {
			NeuroSendActionResult(id.c_str(), false, firstTenMessage);
			return -1;
		}

		char pageButton[16];
		sprintf_s(pageButton, "page%d", wantedOffset / kVisibleSlots);
		if (!ProbeScreenButtonReady(pageButton, "slot page button") ||
		    !DispatchScreenButton(pageButton, "slot page dispatch")) {
			NeuroSendActionResult(id.c_str(), false,
			                      "Could not reach the page that slot is on.");
			return -1;
		}

		// The list redraws well after the press, and a press does not always
		// land. Rather than trust it, read back where the window actually ended
		// up; getting this wrong touches a different slot than the one asked for.
		Sleep(kPageChangeSettleMs);
		if (!ProbeSaveWindow(offset, page) || offset != wantedOffset) {
			NeuroSendActionResult(
			    id.c_str(), false,
			    "The screen did not move to the page that slot is on.");
			return -1;
		}
	}
	return bookmark - offset;
}

bool PressSlot(int position, char const *tag) {
	char itemButton[16];
	sprintf_s(itemButton, "item%d%d", position / kSlotColumns,
	          position % kSlotColumns);
	std::string const label(tag);
	return ProbeScreenButtonReady(itemButton, (label + " button").c_str()) &&
	       DispatchScreenButton(itemButton, (label + " dispatch").c_str());
}

// Both slot actions number from 1, like the list their context message sends and
// like select_choice. kag's bookmark index is 0-based.
bool ParseSlot(std::string const &id,
               std::string const &data,
               char const *action,
               int &bookmark) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"slot\"", number) || number < 1) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " requires an integer \"slot\" of 1 or more.")
		        .c_str());
		return false;
	}
	bookmark = number - 1;
	return true;
}

void HandleLoadSaveAction(std::string const &id, std::string const &data) {
	int bookmark = -1;
	if (!ParseSlot(id, data, "load_save", bookmark)) {
		return;
	}

	if (!ProbeStateIs("load", "load_save state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Saves can only be loaded from the load screen.");
		return;
	}
	if (!ProbeBookmarkExists(bookmark, "load_save exists")) {
		NeuroSendActionResult(id.c_str(), false, "That slot is empty.");
		return;
	}

	int const position = MoveToBookmark(
	    id, bookmark,
	    bookmark < kQuickSaveCount
	        ? "That is a quick save slot; open the Quick tab first."
	        : "That slot is on the normal list; leave the Quick tab first.");
	if (position < 0) {
		return;
	}
	if (!PressSlot(position, "load_save slot")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That slot is not selectable right now.");
		return;
	}

	// Pressing a slot that will not load does nothing at all -- no dialog, no
	// state change -- so silence is the failure signal. A load that took either
	// raises the confirmation or, when SystemConfig.askload is off, lands
	// straight in play.
	Sleep(kDialogSettleMs);
	if (!ProbeLoadAccepted("load_save accepted")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Selecting that slot had no effect.");
		return;
	}
	NeuroSendActionResult(
	    id.c_str(), true,
	    "Selected the save; confirm or cancel the dialog to load it.");
}

void HandleSaveGameAction(std::string const &id, std::string const &data) {
	int bookmark = -1;
	if (!ParseSlot(id, data, "save_game", bookmark)) {
		return;
	}

	if (!ProbeStateIs("save", "save_game state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Saving is only possible from the save screen.");
		return;
	}
	// Empty slots only. An occupied slot raises an overwrite confirmation and
	// destroys a save someone deliberately made; that is not something to hand
	// out until there is a decision about whether to allow it at all.
	if (ProbeBookmarkExists(bookmark, "save_game occupied")) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "That slot already holds a save, and overwriting is not allowed. "
		    "Pick an empty slot.");
		return;
	}

	int const position = MoveToBookmark(
	    id, bookmark,
	    bookmark < kQuickSaveCount
	        ? "That is a quick save slot; open the Quick tab first."
	        : "That slot is on the normal list; leave the Quick tab first.");
	if (position < 0) {
		return;
	}
	if (!PressSlot(position, "save_game slot")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That slot is not selectable right now.");
		return;
	}

	// An empty slot writes immediately -- no confirmation, unlike overwriting --
	// so the slot turning occupied is the proof it worked.
	Sleep(kDialogSettleMs);
	if (!ProbeBookmarkExists(bookmark, "save_game written")) {
		NeuroSendActionResult(id.c_str(), false, "The save did not go through.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Saved the game to that slot.");
}

void HandlePlayVoiceAction(std::string const &id, std::string const &data) {
	int bookmark = -1;
	if (!ParseSlot(id, data, "play_voice", bookmark)) {
		return;
	}

	if (!ProbeStateIs("voice", "play_voice state")) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "Voice lines can only be replayed from the voice screen.");
		return;
	}
	if (!ProbeVoiceExists(bookmark, "play_voice exists")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "There is no recorded line in that slot.");
		return;
	}

	// The first ten entries sit on a page the page buttons cannot reach, the
	// same way the quick saves do on the save list.
	int const position = MoveToBookmark(
	    id, bookmark,
	    "That line is on the first page, which the page buttons cannot reach.");
	if (position < 0) {
		return;
	}
	if (!PressSlot(position, "play_voice slot")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That line is not selectable right now.");
		return;
	}
	// Replaying only plays audio: no dialog, no screen change, nothing to wait
	// for and nothing to undo.
	NeuroSendActionResult(id.c_str(), true, "Replaying that line.");
}

void HandleGoBackAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "go_back does not accept arguments.");
		return;
	}

	// The menu screens all carry a plain "back" control. Confirmed 2026-08-19:
	// kag.current.names["back"] returns to the screen's origin without the
	// "Back To Title" confirmation dialog documented separately.
	bool const ok = ProbeStateIsMenuScreen("go_back state") &&
	                ProbeScreenButtonReady("back", "go_back button") &&
	                DispatchScreenButton("back", "go_back dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, "Going back is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Returned to the previous screen.");
}

void HandleNeuroAction(std::string const &id,
                       std::string const &name,
                       std::string const &data) {
	if (id.empty()) {
		Log("[Action] Ignoring action without an ID\n");
		return;
	}
	if (!ClaimActionId(id)) {
		Log("[Action] Ignoring duplicate action ID\n");
		return;
	}
	if (name == "continue_game") {
		HandleContinueAction(id, data);
	} else if (name == "advance_text") {
		HandleAdvanceTextAction(id, data);
	} else if (name == "select_choice") {
		HandleSelectChoiceAction(id, data);
	} else if (name == "open_menu") {
		HandleOpenMenuAction(id, data);
	} else if (name == "open_quick_menu") {
		HandleOpenQuickMenuAction(id, data);
	} else if (name == "close_quick_menu") {
		HandleCloseQuickMenuAction(id, data);
	} else if (name == "return_to_title") {
		HandleReturnToTitleAction(id, data);
	} else if (name == "answer_dialog") {
		HandleAnswerDialogAction(id, data);
	} else if (name == "go_back") {
		HandleGoBackAction(id, data);
	} else if (name == "hide_text_window") {
		HandleHideTextWindowAction(id, data);
	} else if (name == "show_text_window") {
		HandleShowTextWindowAction(id, data);
	} else if (name == "open_load") {
		HandleOpenLoadAction(id, data);
	} else if (name == "load_save") {
		HandleLoadSaveAction(id, data);
	} else if (name == "play_voice") {
		HandlePlayVoiceAction(id, data);
	} else if (name == "save_game") {
		HandleSaveGameAction(id, data);
	} else if (name == "replay_voice") {
		HandleQuickMenuToggle(id, data, "replay_voice", "vreplay",
		                      "Playing that line again.",
		                      "There is no voice line to replay.");
	} else if (name == "previous_scene") {
		HandleSceneStepAction(id, data, "previous_scene", "xprev",
		                      "Asked to go back to the previous scene break; "
		                      "confirm or cancel the dialog.",
		                      "Went back to the previous scene break.",
		                      "There is nothing to go back to.");
	} else if (name == "next_scene") {
		HandleSceneStepAction(id, data, "next_scene", "next",
		                      "Asked to jump ahead to the next scene break; "
		                      "confirm or cancel the dialog.",
		                      "Jumped ahead to the next scene break.",
		                      "There is nothing to jump ahead to.");
	} else if (name == "open_save") {
		HandleQuickMenuToggle(id, data, "open_save", "save",
		                      "Opened the save screen.",
		                      "Opening the save screen is unavailable.");
	} else if (name == "backlog_page_up") {
		HandleScreenButtonAction(id, data, "backlog_page_up", "backlog", "pageup",
		                         "Scrolled the backlog up.",
		                         "The backlog is already at the top.");
	} else if (name == "backlog_page_down") {
		HandleScreenButtonAction(id, data, "backlog_page_down", "backlog",
		                         "pagedown", "Scrolled the backlog down.",
		                         "The backlog is already at the bottom.");
	} else if (name == "show_scene_list") {
		HandleScreenButtonAction(id, data, "show_scene_list", "backlog", "modechg",
		                         "Switched to the scene list.",
		                         "Cannot switch the backlog view.");
	} else if (name == "show_backlog_lines") {
		HandleScreenButtonAction(id, data, "show_backlog_lines", "backlog",
		                         "modechg", "Switched back to the lines.",
		                         "Cannot switch the backlog view.");
	} else if (name == "jump_to_line") {
		HandleBacklogLineAction(id, data, "jump_to_line", "jump", true,
		                        "Asked to go back to that line; confirm or cancel "
		                        "the dialog.");
	} else if (name == "replay_backlog_voice") {
		HandleBacklogLineAction(id, data, "replay_backlog_voice", "vreplay", false,
		                        "Replaying that line.");
	} else if (name == "read_current_line") {
		HandleReadCurrentLineAction(id, data);
	} else if (name == "read_backlog") {
		HandleReadBacklogAction(id, data);
	} else if (name == "backlog_top") {
		HandleScreenButtonAction(id, data, "backlog_top", "backlog", "top",
		                         "Jumped back in the backlog.",
		                         "There is nothing further back.");
	} else if (name == "backlog_end") {
		HandleScreenButtonAction(id, data, "backlog_end", "backlog", "end",
		                         "Jumped forward in the backlog.",
		                         "There is nothing further forward.");
	} else if (name == "open_backlog") {
		HandleOpenBacklogAction(id, data);
	} else if (name == "start_auto_mode") {
		HandleQuickMenuToggle(id, data, "start_auto_mode", "auto",
		                      "Auto mode is on; the text advances on its own.",
		                      "Turning auto mode on is unavailable.");
	} else if (name == "stop_auto_mode") {
		HandleQuickMenuToggle(id, data, "stop_auto_mode", "auto",
		                      "Auto mode is off.",
		                      "Turning auto mode off is unavailable.");
	} else if (name == "start_skip_mode") {
		HandleQuickMenuToggle(id, data, "start_skip_mode", "skip",
		                      "Skipping through the text.",
		                      "Turning skip mode on is unavailable.");
	} else if (name == "stop_skip_mode") {
		HandleQuickMenuToggle(id, data, "stop_skip_mode", "skip",
		                      "Stopped skipping.",
		                      "Turning skip mode off is unavailable.");
	} else {
		NeuroSendActionResult(id.c_str(), false, "Unsupported action.");
	}
}

// Lists the saved games as Neuro will have to refer to them. Sent on entering
// the load screen rather than on demand: she has no way to look at the slots,
// and a number without a scene name behind it is not a choice.
// Which slots save_game will take. Occupied ones are refused, so listing the
// saves themselves here would only invite refusals.
// What is in the voice list. Each row carries the speaker and the scene it is
// from, which is what makes a slot number mean anything.
void SendVoiceListContext() {
	std::string list;
	int total = 0;
	if (!ProbeVoiceList(kListedSaves, list, total) || total == 0) {
		return;
	}
	std::string message = "The voice screen is open. Recorded lines:";
	size_t start = 0;
	while (start < list.size()) {
		size_t const lineEnd = list.find('\n', start);
		std::string const line = list.substr(
		    start,
		    lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
		start = lineEnd == std::string::npos ? list.size() : lineEnd + 1;
		size_t const firstTab = line.find('\t');
		size_t const secondTab = firstTab == std::string::npos
		                             ? std::string::npos
		                             : line.find('\t', firstTab + 1);
		if (secondTab == std::string::npos) {
			continue;
		}
		message += " " + line.substr(0, firstTab) + ") " +
		           line.substr(firstTab + 1, secondTab - firstTab - 1) + " in " +
		           line.substr(secondTab + 1) + ";";
	}
	if (total > kListedSaves) {
		message += " and " + std::to_string(total - kListedSaves) + " more.";
	}
	NeuroSendContext(message.c_str(), false);
}

void SendFreeSlotContext() {
	std::string slots;
	int total = 0;
	if (!ProbeFreeSlots(kListedSaves, slots, total)) {
		return;
	}
	if (total == 0) {
		NeuroSendContext("The save screen is open. Every slot is taken, and "
		                 "overwriting is not allowed.",
		                 false);
		return;
	}
	std::string message = "The save screen is open. Free slots:" + slots;
	if (total > kListedSaves) {
		message += " and " + std::to_string(total - kListedSaves) + " more";
	}
	message += ". Occupied slots cannot be overwritten.";
	NeuroSendContext(message.c_str(), false);
}

void SendSaveListContext() {
	std::string list;
	int total = 0;
	if (!ProbeBookmarkList(kListedSaves, list, total)) {
		return;
	}
	if (total == 0) {
		NeuroSendContext("The load screen is open. There are no saved games.",
		                 false);
		return;
	}

	std::string message = "The load screen is open. Saved games:";
	size_t start = 0;
	while (start < list.size()) {
		size_t const lineEnd = list.find('\n', start);
		std::string const line = list.substr(
		    start, lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
		start = lineEnd == std::string::npos ? list.size() : lineEnd + 1;

		size_t const firstTab = line.find('\t');
		size_t const secondTab = firstTab == std::string::npos
		                             ? std::string::npos
		                             : line.find('\t', firstTab + 1);
		if (secondTab == std::string::npos) {
			continue;
		}
		message += " " + line.substr(0, firstTab) + ") " +
		           line.substr(firstTab + 1, secondTab - firstTab - 1) + ", " +
		           line.substr(secondTab + 1) + ";";
	}
	if (total > kListedSaves) {
		message += " and " + std::to_string(total - kListedSaves) + " more.";
	}
	NeuroSendContext(message.c_str(), false);
}

void SyncCapabilities(int state) {
	if (state == g_SyncedCapabilityState.load(std::memory_order_acquire)) {
		return;
	}
	if (state == kCapabilityStateTitleLoading) {
		// No action can succeed yet, so advertise none. The explanatory context
		// is not sent here: this state is entered during plugin init, before the
		// SDK connects, and NeuroSendContext drops anything sent while not ready.
		// RefreshPrototypeActionState retries it until it lands.
		NeuroSetRegisteredActions(nullptr, 0);
		Log("[Action] Capabilities: title-loading (none)\n");
	} else if (state == kCapabilityStateTitle) {
		NeuroRegisteredAction const actions[] = {
		    {"continue_game", "Continue the current game.", kContinueGameSchema},
		    {"open_load", "Open the load screen to pick a saved game.",
		     kOpenLoadSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		// Not silent: the menu becoming usable is the moment there is finally
		// something to do.
		NeuroSendContext("The main menu is ready.", false);
		Log("[Action] Capabilities: title (continue_game, open_load)\n");
	} else if (state == kCapabilityStateTitleNoSave) {
		NeuroRegisteredAction const actions[] = {
		    {"open_load", "Open the load screen to pick a saved game.",
		     kOpenLoadSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		NeuroSendContext("The main menu is ready. There is no game in progress to "
		                 "continue, so a save has to be loaded.",
		                 false);
		Log("[Action] Capabilities: title-no-save (open_load)\n");
	} else if (state >= kCapabilityStatePlayBase &&
	           state < kCapabilityStateBacklogBase) {
		bool const choice = (state & kPlayFlagChoice) != 0;
		bool const panelOut = (state & kPlayFlagPanelOut) != 0;
		bool const autoMode = (state & kPlayFlagAuto) != 0;
		bool const skipMode = (state & kPlayFlagSkip) != 0;
		bool const canReplayVoice = (state & kPlayFlagVoice) != 0;
		bool const canGoPrevious = (state & kPlayFlagPrevious) != 0;
		bool const canGoNext = (state & kPlayFlagNext) != 0;
		std::vector<NeuroRegisteredAction> actions;
		if (choice) {
			actions.push_back({"select_choice",
			                   "Choose a dialogue option by its number in the "
			                   "list, counting from 1.",
			                   kSelectChoiceSchema});
		} else {
			actions.push_back(
			    {"advance_text", "Advance the current dialogue.", kAdvanceTextSchema});
		}
		// The quick menu is reachable during a choice too, so it is offered in
		// every play state -- only its direction follows the bar's position.
		// open_menu rides with it: the settings screen is reached by the bar's
		// own option button, so offering it while the bar is retracted meant an
		// action with nothing on screen behind it.
		if (panelOut) {
			actions.push_back({"close_quick_menu", "Retract the quick menu bar.",
			                   kCloseQuickMenuSchema});
			actions.push_back(
			    {"open_menu", "Open the settings menu.", kOpenMenuSchema});
		// Always available while a line is on screen: it is how the same line is
		// re-read in the original Japanese, which the context feed never carries.
		actions.push_back({"read_current_line",
		                   "Read the line currently on screen. Pass language "
		                   "\"japanese\" for the original wording.",
		                   kReadLineSchema});
			actions.push_back({"hide_text_window",
			                   "Hide the text window so only the artwork is on "
			                   "screen.",
			                   kHideTextWindowSchema});
			actions.push_back({"open_load",
			                   "Open the load screen to pick a saved game.",
			                   kOpenLoadSchema});
			actions.push_back({"open_save",
			                   "Open the save screen to store progress in a slot.",
			                   kOpenSaveSchema});
			actions.push_back({"open_backlog",
			                   "Open the backlog to re-read earlier lines.",
			                   kOpenBacklogSchema});
			// Both are the same bar button pressed twice, so only the direction
			// that would actually change something is offered.
			if (autoMode) {
				actions.push_back({"stop_auto_mode",
				                   "Turn off auto mode so the text stops advancing on "
				                   "its own.",
				                   kStopAutoModeSchema});
			} else {
				actions.push_back({"start_auto_mode",
				                   "Turn on auto mode so the text advances on its own.",
				                   kStartAutoModeSchema});
			}
			if (skipMode) {
				actions.push_back({"stop_skip_mode",
				                   "Stop skipping and read normally again.",
				                   kStopSkipModeSchema});
			} else {
				actions.push_back({"start_skip_mode",
				                   "Skip quickly through the text until it is turned "
				                   "off or a choice appears.",
				                   kStartSkipModeSchema});
			// The game already decides when these apply -- a voice to replay, a scene
			// history to step back into, a next stop to jump to -- and the bar shows
			// it in the buttons. Offering them otherwise would advertise a dead press.
			if (canReplayVoice) {
				actions.push_back({"replay_voice",
				                   "Play the current line's voice again.",
				                   kReplayVoiceSchema});
			}
			if (canGoPrevious) {
				actions.push_back({"previous_scene",
				                   "Go back to the previous scene break.",
				                   kPreviousSceneSchema});
			}
			if (canGoNext) {
				actions.push_back({"next_scene",
				                   "Jump ahead to the next scene break, the same place "
				                   "skipping would stop at.",
				                   kNextSceneSchema});
			}
			}
		} else {
			actions.push_back(
			    {"open_quick_menu",
			     "Slide out the quick menu bar so its buttons are visible.",
			     kOpenQuickMenuSchema});
		}
		NeuroSetRegisteredActions(actions.data(),
		                          static_cast<int>(actions.size()));
		std::string summary = "[Action] Capabilities: ";
		summary += choice ? "choice" : "game";
		if (panelOut) summary += "+panel";
		summary += " (";
		for (size_t i = 0; i < actions.size(); ++i) {
			if (i) summary += ", ";
			summary += actions[i].name;
		}
		summary += ")\n";
		Log(summary.c_str());
	} else if (state == kCapabilityStateTextHidden) {
		NeuroRegisteredAction const actions[] = {
		    {"show_text_window",
		     "Bring the text window back so reading can continue.",
		     kShowTextWindowSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		Log("[Action] Capabilities: text-hidden (show_text_window)\n");
	} else if (state == kCapabilityStateDialog) {
		NeuroRegisteredAction const actions[] = {
		    {"answer_dialog",
		     "Answer the confirmation dialog that is currently on screen: "
		     "confirm true to accept, false to cancel.",
		     kAnswerDialogSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		Log("[Action] Capabilities: dialog (answer_dialog)\n");
	} else if (state == kCapabilityStateMenu ||
	           state == kCapabilityStateMenuBackOnly) {
		std::vector<NeuroRegisteredAction> actions;
		actions.push_back({"go_back",
		                   "Leave the current screen and return to the one it was "
		                   "opened from.",
		                   kGoBackSchema});
		if (state == kCapabilityStateMenu) {
			actions.push_back({"return_to_title",
			                   "Leave the game and go back to the title screen. Asks "
			                   "for confirmation first.",
			                   kReturnToTitleSchema});
		}
		NeuroSetRegisteredActions(actions.data(),
		                          static_cast<int>(actions.size()));
		Log(state == kCapabilityStateMenu
		        ? "[Action] Capabilities: menu (go_back, return_to_title)\n"
		        : "[Action] Capabilities: menu-back-only (go_back)\n");
	} else if (state == kCapabilityStateLoad) {
		NeuroRegisteredAction const actions[] = {
		    {"go_back",
		     "Leave the current screen and return to the one it was opened from.",
		     kGoBackSchema},
		    {"return_to_title",
		     "Leave the game and go back to the title screen. Asks for "
		     "confirmation first.",
		     kReturnToTitleSchema},
		    {"load_save",
		     "Load a saved game by its slot number, as listed when this screen "
		     "opened. Asks for confirmation first.",
		     kLoadSaveSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		SendSaveListContext();
		Log("[Action] Capabilities: load (go_back, return_to_title, load_save)\n");
	} else if (state == kCapabilityStateSave) {
		NeuroRegisteredAction const actions[] = {
		    {"go_back",
		     "Leave the current screen and return to the one it was opened from.",
		     kGoBackSchema},
		    {"return_to_title",
		     "Leave the game and go back to the title screen. Asks for "
		     "confirmation first.",
		     kReturnToTitleSchema},
		    {"save_game",
		     "Save the current progress into an empty slot, by the slot number "
		     "listed when this screen opened. Occupied slots are refused.",
		     kSaveGameSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		SendFreeSlotContext();
		Log("[Action] Capabilities: save (go_back, return_to_title, save_game)\n");
	} else if (state >= kCapabilityStateBacklogBase) {
		// Only on the way in, not on every scroll: the flags change constantly
		// while paging, and the total does not.
		if (g_SyncedCapabilityState.load(std::memory_order_acquire) <
		    kCapabilityStateBacklogBase) {
			int total = 0;
			if (ProbeHistoryCount(total) && total > 0) {
				NeuroSendContext(("The backlog is open. It holds " +
				                  std::to_string(total) +
				                  " lines; read_backlog reads four at a time.")
				                     .c_str(),
				                 false);
			}
		}
		std::vector<NeuroRegisteredAction> actions;
		actions.push_back({"go_back", "Close the backlog and return to the game.",
		                   kGoBackSchema});
		actions.push_back({"read_backlog",
		                   "Read four lines of the conversation history. Give "
		                   "\"from\" to start at a line number, or leave it out "
		                   "for the four most recent.",
		                   kReadBacklogSchema});
		if ((state & kBacklogFlagPageUp) != 0) {
			actions.push_back({"backlog_page_up",
			                   "Scroll the backlog up one page.",
			                   kBacklogPageUpSchema});
		}
		if ((state & kBacklogFlagPageDown) != 0) {
			actions.push_back({"backlog_page_down",
			                   "Scroll the backlog down one page.",
			                   kBacklogPageDownSchema});
		}
		if ((state & kBacklogFlagTop) != 0) {
			actions.push_back({"backlog_top",
			                   "Jump back to the previous scene break in the "
			                   "backlog, or to its start.",
			                   kBacklogTopSchema});
		}
		if ((state & kBacklogFlagEnd) != 0) {
			actions.push_back({"backlog_end",
			                   "Jump forward to the next scene break in the "
			                   "backlog, or to its end.",
			                   kBacklogEndSchema});
		}
		if ((state & kBacklogFlagSceneList) != 0) {
			actions.push_back({"show_backlog_lines",
			                   "Switch the backlog back to the list of lines.",
			                   kShowBacklogLinesSchema});
		} else {
			actions.push_back({"show_scene_list",
			                   "Switch the backlog to the scene list.",
			                   kShowSceneListSchema});
		}
		// The numbering is window-relative, so it is spelled out in the
		// description each time rather than left for Neuro to infer: after a
		// scroll the same number is a different line.
		int const jumpMask = (state >> kBacklogJumpShift) & kBacklogSlotMask;
		int const voiceMask = (state >> kBacklogVoiceShift) & kBacklogSlotMask;
		std::string jumpList;
		std::string voiceList;
		for (int slot = 0; slot < 5; ++slot) {
			if ((jumpMask & (1 << slot)) != 0) {
				jumpList += (jumpList.empty() ? "" : ", ") + std::to_string(slot + 1);
			}
			if ((voiceMask & (1 << slot)) != 0) {
				voiceList += (voiceList.empty() ? "" : ", ") + std::to_string(slot + 1);
			}
		}
		std::string jumpText;
		std::string voiceText;
		if (!jumpList.empty()) {
			// In scene mode the same control jumps to whole scenes, so the
			// wording follows the list that is actually showing.
			jumpText = std::string("Go back to one of the ") +
			           ((state & kBacklogFlagSceneList) != 0 ? "scenes"
			                                                 : "lines") +
			           " on screen, numbered from the top. Available now: " +
			           jumpList + ". Asks for confirmation first.";
			actions.push_back({"jump_to_line", jumpText.c_str(), kJumpToLineSchema});
		}
		if (!voiceList.empty()) {
			voiceText = "Replay the voice of one of the lines on screen, numbered "
			            "from the top. Available now: " + voiceList + ".";
			actions.push_back(
			    {"replay_backlog_voice", voiceText.c_str(), kReplayBacklogVoiceSchema});
		}
		NeuroSetRegisteredActions(actions.data(),
		                          static_cast<int>(actions.size()));
		std::string summary = "[Action] Capabilities: backlog (";
		for (size_t i = 0; i < actions.size(); ++i) {
			if (i) summary += ", ";
			summary += actions[i].name;
		}
		summary += ")\n";
		Log(summary.c_str());
	} else if (state == kCapabilityStateVoice) {
		NeuroRegisteredAction const actions[] = {
		    {"go_back",
		     "Leave the current screen and return to the one it was opened from.",
		     kGoBackSchema},
		    {"play_voice",
		     "Replay a recorded line by its slot number, as listed when this "
		     "screen opened.",
		     kPlayVoiceSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		SendVoiceListContext();
		Log("[Action] Capabilities: voice (go_back, play_voice)\n");
	} else if (state == kCapabilityStateBare) {
		NeuroSetRegisteredActions(nullptr, 0);
		Log("[Action] Capabilities: bare (none)\n");
	}
	g_SyncedCapabilityState.store(state, std::memory_order_release);
}

}  // namespace

void InitializePrototypeActionRouter() {
	NeuroSetActionHandler(HandleNeuroAction);
	// Init runs during the startup splashes, so nothing is dispatchable yet;
	// the monitor promotes this to the real title state once the menu is usable.
	SyncCapabilities(kCapabilityStateTitleLoading);
}

void ShutdownPrototypeActionRouter() {
	NeuroSetActionHandler(nullptr);
	g_ContinueActionInFlight.store(false, std::memory_order_release);
	g_SyncedCapabilityState.store(kCapabilityStateUnknown,
	                              std::memory_order_release);
	g_TitleLoadingNoticeSent.store(false, std::memory_order_release);
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	g_ActionIds.clear();
	g_ActionIdSet.clear();
}

// Retried every poll until accepted: the loading state starts before the SDK
// connects, and NeuroSendContext drops anything sent while not ready rather
// than queueing it, so a single send on entering the state would be lost.
void TrySendTitleLoadingNotice() {
	if (g_TitleLoadingNoticeSent.load(std::memory_order_acquire)) {
		return;
	}
	if (NeuroSendContext(
	        "The game is starting up. The main menu is not available yet.",
	        true)) {
		g_TitleLoadingNoticeSent.store(true, std::memory_order_release);
	}
}

void RefreshPrototypeActionState() {
	// One probe per poll: this runs at 5 Hz against the game main thread, so
	// extra round trips here would be pure contention for no new information.
	ScreenSnapshot screen;
	if (!ProbeScreen(screen)) {
		return;
	}

	// A modal dialog owns the screen: answering it is the only thing that can
	// succeed, and leaving the other actions registered would invite calls that
	// are certain to fail. Checked before the state branches for that reason.
	if (screen.dialogVisible) {
		SyncCapabilities(kCapabilityStateDialog);
		return;
	}

	// Every distinct state value is logged once. A screen this code has never
	// seen used to fall through every branch and return without a trace, which
	// was indistinguishable from "capabilities are simply unchanged".
	static std::string lastObservedState;
	bool const stateChanged = screen.state != lastObservedState;
	if (stateChanged) {
		lastObservedState = screen.state;
		Log(("[Action] Observed state: '" + screen.state + "'\n").c_str());
	}

	if (screen.state == "title") {
		// Readiness is the Load button, not Continue: it is on the title screen
		// whether or not a suspend-save exists, so gating on Continue alone left
		// a fresh install stuck in title-loading with nothing registered.
		// Re-probed only until a title set is registered; the steady state costs
		// nothing, and coming back from play re-enters through another state.
		int const synced = g_SyncedCapabilityState.load(std::memory_order_acquire);
		if (synced == kCapabilityStateTitle ||
		    synced == kCapabilityStateTitleNoSave) {
			SyncCapabilities(synced);
		} else if (!ProbeScreenButtonReady("load", "title load button")) {
			SyncCapabilities(kCapabilityStateTitleLoading);
			TrySendTitleLoadingNotice();
		} else if (ProbeCanContinue("title canContinue") &&
		           ProbeScreenButtonReady("cont", "title button")) {
			SyncCapabilities(kCapabilityStateTitle);
		} else {
			SyncCapabilities(kCapabilityStateTitleNoSave);
		}
	} else if (screen.state.empty()) {
		// Still in the startup splashes: SystemHook.currentState is genuinely an
		// empty string until the title exists. This is the bulk of the wait, so
		// the notice has to cover it -- restricting it to the brief
		// title-but-not-ready window made it arrive just as the menu appeared,
		// which read as nonsense ("not available yet" the moment it was).
		//
		// Only believe an empty reading before any real state has been seen.
		// currentState is not a stable field: it was observed holding unrelated
		// values mid-session (e.g. "bgmPlaying" while the option screen was up),
		// so treating a stray empty reading as "back to loading" would revoke the
		// live action set for no reason.
		int const synced = g_SyncedCapabilityState.load(std::memory_order_acquire);
		if (synced == kCapabilityStateUnknown ||
		    synced == kCapabilityStateTitleLoading) {
			SyncCapabilities(kCapabilityStateTitleLoading);
			TrySendTitleLoadingNotice();
		}
	} else if (screen.state == "game") {
		// Nothing else can succeed while the window is away: the bar's buttons
		// are gone with it, and a click restores the window instead of advancing.
		if (screen.textWindowHidden) {
			SyncCapabilities(kCapabilityStateTextHidden);
			return;
		}
		int play = kCapabilityStatePlayBase;
		if (screen.choiceVisible) play |= kPlayFlagChoice;
		if (screen.quickMenuOut) {
			play |= kPlayFlagPanelOut;
			// The mode toggles live on the bar, so they only change the action set
			// while it is out. Folding them in unconditionally would re-register an
			// identical set every time auto or skip flipped behind a retracted bar.
			if (screen.autoMode) play |= kPlayFlagAuto;
			if (screen.skipMode) play |= kPlayFlagSkip;
			if (screen.canReplayVoice) play |= kPlayFlagVoice;
			if (screen.canGoPrevious) play |= kPlayFlagPrevious;
			if (screen.canGoNext) play |= kPlayFlagNext;
		}
		SyncCapabilities(play);
	} else if (screen.state == "load") {
		SyncCapabilities(kCapabilityStateLoad);
	} else if (screen.state == "save") {
		SyncCapabilities(kCapabilityStateSave);
	} else if (screen.state == "backlog") {
		int flags = 0;
		int jumpMask = 0;
		int voiceMask = 0;
		if (ProbeBacklogButtons(flags) &&
		    ProbeBacklogLineButtons(jumpMask, voiceMask)) {
			SyncCapabilities(kCapabilityStateBacklogBase | flags |
			                 (jumpMask << kBacklogJumpShift) |
			                 (voiceMask << kBacklogVoiceShift));
		}
	} else if (screen.state == "voice") {
		SyncCapabilities(kCapabilityStateVoice);
	} else {
		// Anything that is neither the title nor play. Rather than matching a
		// list of screen names -- which kept missing ones, and left the previous
		// actions registered when it did -- ask the screen which controls it
		// carries. The backlog has "back" but no "title"; option/load/save carry
		// both; cgmode, scenemode, soundmode, voicemode and search have not been
		// visited yet and resolve themselves the same way.
		//
		// Re-probed only when the state string changes, so sitting on a screen
		// costs nothing beyond the one snapshot.
		int const synced = g_SyncedCapabilityState.load(std::memory_order_acquire);
		bool const settled = synced == kCapabilityStateMenu ||
		                     synced == kCapabilityStateMenuBackOnly ||
		                     synced == kCapabilityStateBare;
		if (settled && !stateChanged) {
			SyncCapabilities(synced);
		} else if (!ProbeScreenButtonReady("back", "screen back button")) {
			SyncCapabilities(kCapabilityStateBare);
		} else if (ProbeScreenButtonReady("title", "screen title button")) {
			SyncCapabilities(kCapabilityStateMenu);
		} else {
			SyncCapabilities(kCapabilityStateMenuBackOnly);
		}
	}

	if (!g_ContinueActionInFlight.load(std::memory_order_acquire)) {
		return;
	}
	if (screen.state != "title" ||
	    GetTickCount64() -
	            g_ContinueActionStartedAt.load(std::memory_order_acquire) >=
	        5000) {
		g_ContinueActionInFlight.store(false, std::memory_order_release);
		Log("[Action] Continue transition completed or timed out\n");
	}
}
