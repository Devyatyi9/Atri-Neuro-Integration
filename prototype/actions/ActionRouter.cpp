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

std::atomic<int> g_SyncedCapabilityState(-1);
// Whether the "still starting up" notice reached the server. It cannot simply
// be sent once on entering the loading state, because that happens before the
// SDK connects.
std::atomic<bool> g_TitleLoadingNoticeSent(false);
constexpr int kCapabilityStateUnknown = -1;
constexpr int kCapabilityStateTitle = 0;
constexpr int kCapabilityStateGame = 1;
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
// The four play states below are the same screen with two independent flags:
// whether a choice is up, and whether the quick menu bar is out. They are
// separate states only so the dedup in SyncCapabilities still works on a single
// int; the action list itself is composed in one place.
//
// Choice vs text is a disposable action window in the sense of the SDK's own
// guidance (neuro-sdk/API/README.md, "Unregister disposable actions before
// sending result"): while the select layer is up, advancing the text does
// nothing, and select_choice stops being answerable the moment the layer
// closes. Offering both at once advertised one action that was certain to fail
// either way. The quick menu splits the same way -- the bar is either out or
// retracted, so only one of open/close can apply. Everything that stays valid
// throughout, including reaching the bar itself during a choice, is offered in
// all four.
constexpr int kCapabilityStateChoice = 5;
constexpr int kCapabilityStateGamePanel = 6;
constexpr int kCapabilityStateChoicePanel = 7;
// The text window hidden away by the bar's hide button: the artwork is all
// that is left on screen, the hide button itself goes enabled = 0, and a click
// brings the window back instead of advancing. Only one action can apply.
constexpr int kCapabilityStateTextHidden = 8;
// The title screen with no suspend-save to continue from. Loading a save is
// still possible there, so it cannot be folded into title-loading.
constexpr int kCapabilityStateTitleNoSave = 9;

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
	// menu-navigation.md.
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
	} else {
		NeuroSendActionResult(id.c_str(), false, "Unsupported action.");
	}
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
	} else if (state == kCapabilityStateGame ||
	           state == kCapabilityStateGamePanel ||
	           state == kCapabilityStateChoice ||
	           state == kCapabilityStateChoicePanel) {
		bool const choice = state == kCapabilityStateChoice ||
		                   state == kCapabilityStateChoicePanel;
		bool const panelOut = state == kCapabilityStateGamePanel ||
		                      state == kCapabilityStateChoicePanel;
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
			actions.push_back({"hide_text_window",
			                   "Hide the text window so only the artwork is on "
			                   "screen.",
			                   kHideTextWindowSchema});
			actions.push_back({"open_load",
			                   "Open the load screen to pick a saved game.",
			                   kOpenLoadSchema});
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
	} else if (state == kCapabilityStateMenu) {
		NeuroRegisteredAction const actions[] = {
		    {"go_back",
		     "Return to the previous screen from the current menu (options, "
		     "save, load, or voice).",
		     kGoBackSchema},
		    {"return_to_title",
		     "Leave the game and go back to the title screen. Asks for "
		     "confirmation first.",
		     kReturnToTitleSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		Log("[Action] Capabilities: menu (go_back, return_to_title)\n");
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

	// A state this function does not recognise falls through every branch below
	// and returns without a trace, which is indistinguishable from "capabilities
	// are simply unchanged". Log each distinct value once so an unexpected or
	// empty state is visible instead of silent.
	{
		static std::string lastObservedState;
		if (screen.state != lastObservedState) {
			lastObservedState = screen.state;
			Log(("[Action] Observed state: '" + screen.state + "'\n").c_str());
		}
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
		if (screen.choiceVisible) {
			SyncCapabilities(screen.quickMenuOut ? kCapabilityStateChoicePanel
			                                     : kCapabilityStateChoice);
		} else {
			SyncCapabilities(screen.quickMenuOut ? kCapabilityStateGamePanel
			                                     : kCapabilityStateGame);
		}
	} else if (screen.state == "option" || screen.state == "load" ||
	           screen.state == "save" || screen.state == "voice") {
		SyncCapabilities(kCapabilityStateMenu);
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
