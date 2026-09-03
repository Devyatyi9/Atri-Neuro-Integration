#include "ScreenTitle.h"

#include <windows.h>

#include <atomic>
#include <string>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "ActionSet.h"
#include "Logging.h"
#include "NeuroClient.h"

namespace {

// The three conditional entries, kept as two flags each: the game expresses
// "not now" two different ways, and the difference decides what is offered.
// Continue and Extra are drawn greyed out until they apply, so they are offered
// and refused on press with the reason; the True End is not drawn at all until
// it is unlocked, so it is not offered.
constexpr int kFlagShowsContinue = 1 << 0;
constexpr int kFlagCanContinue = 1 << 1;
constexpr int kFlagShowsExtra = 1 << 2;
constexpr int kFlagCanOpenExtra = 1 << 3;
constexpr int kFlagShowsTrueEnd = 1 << 4;

// Continue leaves the title behind, but not at once: the scene loads for a
// moment while the screen still reads as the title. Without this the refresh
// would re-register the title set over an action already on its way.
std::atomic<bool> g_ContinueInFlight(false);
std::atomic<ULONGLONG> g_ContinueStartedAt(0);
constexpr ULONGLONG kContinueTimeoutMs = 5000;

// Result messages are what Neuro reads back, so each handler says which step
// refused rather than only that the action failed.
void HandleContinueAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "continue_game does not accept arguments.");
		return;
	}
	if (g_ContinueInFlight.exchange(true, std::memory_order_acq_rel)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Continue is already being started.");
		return;
	}
	g_ContinueStartedAt.store(GetTickCount64(), std::memory_order_release);

	// Title screen, a suspend-save tagged "continue", and the button itself
	// visible and dispatchable.
	bool const ok = ProbeStateIs("title", "continue state") &&
	                ProbeCanContinue("continue canContinue") &&
	                ProbeScreenButtonReady("cont", "continue button") &&
	                DispatchScreenButton("cont", "continue dispatch");
	if (!ok) {
		g_ContinueInFlight.store(false, std::memory_order_release);
		// The button is on the title whether or not it can be used, so this is
		// the ordinary answer for pressing a greyed-out entry, not an edge case.
		NeuroSendActionResult(
		    id.c_str(), false,
		    "Continue is greyed out: there is no game in progress yet.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Continue started.");
}

void HandleStartNewGameAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "start_new_game does not accept arguments.");
		return;
	}
	bool const ok = ProbeStateIs("title", "start_new_game state") &&
	                ProbeScreenButtonReady("start", "start_new_game button") &&
	                DispatchScreenButton("start", "start_new_game dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Starting a new game is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Started a new game.");
}

void HandleStartTrueEndAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "start_true_end does not accept arguments.");
		return;
	}
	bool const ok = ProbeStateIs("title", "start_true_end state") &&
	                ProbeScreenButtonReady("tend", "start_true_end button") &&
	                DispatchScreenButton("tend", "start_true_end dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Starting the true end is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Started the true end route.");
}

void HandleExitGameAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "exit_game does not accept arguments.");
		return;
	}
	if (!ProbeStateIs("title", "exit_game state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Quitting is only offered from the title screen.");
		return;
	}
	if (!DispatchExitRequest("exit_game dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "Quitting is unavailable.");
		return;
	}
	Sleep(kDialogSettleMs);
	if (!ProbeDialogVisible("exit_game asked")) {
		NeuroSendActionResult(id.c_str(), false, "The game did not ask to quit.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      "Asked to quit; confirm or cancel the dialog.");
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
		ok = ProbeQuickMenuButtonReady("load", "open_load bar button") &&
		     DispatchQuickMenuButton("load", "open_load bar dispatch");
	}
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the load screen is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the load screen.");
}

void HandleOpenSettingsAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "open_settings does not accept arguments.");
		return;
	}

	// From the title the settings screen has its own button; during play it is
	// reached through the quick menu bar. Same destination, different control.
	if (ProbeStateIs("title", "open_settings title state")) {
		bool const viaTitle =
		    ProbeScreenButtonReady("option", "open_settings title button") &&
		    DispatchScreenButton("option", "open_settings title dispatch");
		NeuroSendActionResult(id.c_str(), viaTitle,
		                      viaTitle ? "Opened the settings screen."
		                               : "Opening the settings screen is "
		                                 "unavailable.");
		return;
	}

	// The bar has to be out before its Option button is pressed: the button
	// reports visible/enabled even while retracted, so without that check the
	// settings screen could open with nothing on screen having been pressed.
	// Pressing the bar's own control rather than calling SystemAction.option()
	// is what keeps the action visible; same destination either way.
	bool const ok = ProbeStateIs("game", "open_settings state") &&
	                ProbeQuickMenuButtonReady("option", "open_settings button") &&
	                DispatchQuickMenuButton("option", "open_settings dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the settings menu is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the settings menu.");
}

}  // namespace

namespace ScreenTitle {

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	if (name == "continue_game") {
		HandleContinueAction(id, data);
	} else if (name == "start_new_game") {
		HandleStartNewGameAction(id, data);
	} else if (name == "start_true_end") {
		HandleStartTrueEndAction(id, data);
	} else if (name == "open_load") {
		HandleOpenLoadAction(id, data);
	} else if (name == "open_settings") {
		HandleOpenSettingsAction(id, data);
	} else if (name == "open_extra") {
		HandleScreenButtonAction(id, data, "open_extra", "title", "extra",
		                         "Opened the Extra screen.",
		                         "Extra is greyed out: it is not unlocked yet.");
	} else if (name == "exit_game") {
		HandleExitGameAction(id, data);
	} else {
		return false;
	}
	return true;
}

void AppendActions(ActionSet &set, int payload) {
	// In the order the buttons sit on screen, so the set reads like the menu it
	// mirrors, with the conditional entries in their own places rather than
	// appended at the end.
	if ((payload & kFlagShowsTrueEnd) != 0) {
		set.add("start_true_end", "Start the True End route from the beginning.",
		        kStartTrueEndSchema);
	}
	if ((payload & kFlagShowsContinue) != 0) {
		set.add("continue_game", "Continue the current game.", kContinueGameSchema);
	}
	set.add("start_new_game", "Start a new game from the beginning.",
	        kStartNewGameSchema);
	set.add("open_load", "Open the load screen to pick a saved game.",
	        kOpenLoadSchema);
	set.add("open_settings", "Open the settings screen.", kOpenSettingsSchema);
	if ((payload & kFlagShowsExtra) != 0) {
		set.add("open_extra",
		        "Open the Extra screen: the picture gallery and the music list.",
		        kOpenExtraSchema);
	}
	set.add("exit_game", "Quit the game. Asks for confirmation first.",
	        kExitGameSchema);
}

void SendEnteringContext(int payload) {
	// Not silent: the menu becoming usable is the moment there is finally
	// something to do. The greyed-out entries are named rather than left to be
	// discovered by pressing them -- they are registered because they are on
	// screen, and only this says they cannot be used yet.
	std::string message = "The main menu is ready.";
	if ((payload & kFlagShowsContinue) != 0 &&
	    (payload & kFlagCanContinue) == 0) {
		message += " Continue is greyed out: there is no game in progress yet.";
	}
	if ((payload & kFlagShowsExtra) != 0 && (payload & kFlagCanOpenExtra) == 0) {
		message += " Extra is greyed out: it is not unlocked yet.";
	}
	// Where Continue would pick up. Hovering the button shows this on screen, but
	// only as a drawn thumbnail with no text on it, so saying it is the only way
	// it reaches her at all.
	if ((payload & kFlagCanContinue) != 0) {
		std::string chapter;
		std::string date;
		if (ProbeContinueSave(chapter, date) && !chapter.empty()) {
			message += " Continue would resume in " + chapter;
			if (!date.empty()) {
				message += ", saved " + date;
			}
			message += ".";
		}
	}
	NeuroSendContext(message.c_str(), false);
}

int ReadPayload(ScreenSnapshot const &screen, bool &ready) {
	// Readiness is the Load button: it is on the title screen whether or not
	// there is anything to continue or unlock.
	ready = screen.titleReady;
	int payload = 0;
	if (screen.titleShowsContinue)
		payload |= kFlagShowsContinue;
	if (screen.titleCanContinue)
		payload |= kFlagCanContinue;
	if (screen.titleShowsExtra)
		payload |= kFlagShowsExtra;
	if (screen.titleCanOpenExtra)
		payload |= kFlagCanOpenExtra;
	if (screen.titleShowsTrueEnd)
		payload |= kFlagShowsTrueEnd;
	return payload;
}

void NoteRefresh(bool onTitle) {
	if (!g_ContinueInFlight.load(std::memory_order_acquire)) {
		return;
	}
	bool const timedOut =
	    GetTickCount64() - g_ContinueStartedAt.load(std::memory_order_acquire) >=
	    kContinueTimeoutMs;
	if (onTitle && !timedOut) {
		return;
	}
	g_ContinueInFlight.store(false, std::memory_order_release);
	Log(onTitle ? "[Action] Continue transition timed out\n"
	            : "[Action] Continue transition completed\n");
}

}  // namespace ScreenTitle
