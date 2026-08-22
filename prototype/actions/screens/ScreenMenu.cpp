#include "ScreenMenu.h"

#include <windows.h>

#include <atomic>
#include <string>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "Logging.h"
#include "NeuroClient.h"

namespace {

// Whether the first-run notice reached the server. It cannot simply be sent
// once on entering that screen: the screen is up while the SDK may still be
// connecting, and a context sent then is dropped rather than queued.
std::atomic<bool> g_LanguageNoticeSent(false);

void HandleSettingsTabAction(std::string const &id, std::string const &data) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"tab\"", number) || number < 1) {
		NeuroSendActionResult(id.c_str(), false,
		                      "settings_tab requires a \"tab\" of 1 or more.");
		return;
	}
	if (!ProbeStateIsMenuScreen("settings_tab state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on a settings screen.");
		return;
	}
	char button[16];
	sprintf_s(button, "page%d", number - 1);
	if (!ProbeScreenButtonReady(button, "settings_tab button") ||
	    !DispatchScreenButton(button, "settings_tab dispatch")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That tab is already open, or there is no such tab.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Switched to that tab.");
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
	// open_quick_menu -> open_settings -> return_to_title and every step is
	// visible.
	//
	// The control's handler is SystemAction.title() -> _title(): it guards on
	// kag.startAnchorEnabled and raises the AskTitle dialog itself.
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
	NeuroSendActionResult(
	    id.c_str(), true,
	    confirm ? "Confirmed the dialog." : "Cancelled the dialog.");
}

void HandleGoBackAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "go_back does not accept arguments.");
		return;
	}

	// The menu screens all carry a plain "back" control, and it returns to the
	// screen's origin without raising the "Back To Title" confirmation.
	bool const ok = ProbeStateIsMenuScreen("go_back state") &&
	                ProbeScreenButtonReady("back", "go_back button") &&
	                DispatchScreenButton("back", "go_back dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, "Going back is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Returned to the previous screen.");
}

void HandleConfirmLanguageAction(std::string const &id,
                                 std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "confirm_language does not accept arguments.");
		return;
	}

	// The same "back" control as every other menu screen, pressed the same way;
	// here it is the OK of the first-run prompt.
	bool const ok = ProbeLanguageScreen("confirm_language screen") &&
	                ProbeScreenButtonReady("back", "confirm_language button") &&
	                DispatchScreenButton("back", "confirm_language dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The language screen is no longer up.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      "Language confirmed. The game is going on to the "
	                      "title screen.");
}

}  // namespace

namespace ScreenMenu {

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	if (name == "go_back") {
		HandleGoBackAction(id, data);
	} else if (name == "return_to_title") {
		HandleReturnToTitleAction(id, data);
	} else if (name == "settings_tab") {
		HandleSettingsTabAction(id, data);
	} else if (name == "answer_dialog") {
		HandleAnswerDialogAction(id, data);
	} else if (name == "confirm_language") {
		HandleConfirmLanguageAction(id, data);
	} else {
		return false;
	}
	return true;
}

void AppendMenuActions(ActionSet &set, int tabs, bool canReturnToTitle) {
	set.add("go_back",
	        "Leave the current screen and return to the one it was opened from.",
	        kGoBackSchema);
	if (canReturnToTitle) {
		set.add("return_to_title",
		        "Leave the game and go back to the title screen. Asks for "
		        "confirmation first.",
		        kReturnToTitleSchema);
	}
	if (tabs <= 0) {
		return;
	}
	// The controls carry no readable captions, so the names are spelled out here
	// in screen order.
	static char const *const kTabNames[] = {
	    "display", "program", "text", "sound", "dialogue", "touch", "shortcuts"};
	constexpr int kKnownTabs =
	    static_cast<int>(sizeof(kTabNames) / sizeof(kTabNames[0]));
	std::string text = "Switch the settings screen to one of its " +
	                   std::to_string(tabs) + " tabs:";
	for (int tab = 0; tab < tabs && tab < kKnownTabs; ++tab) {
		text += " " + std::to_string(tab + 1) + ") " + kTabNames[tab];
	}
	text += ".";
	set.add("settings_tab", set.keep(text),
	        set.keep(BoundedIntSchema("tab", tabs)));
}

void AppendDialogActions(ActionSet &set) {
	set.add("answer_dialog",
	        "Answer the confirmation dialog that is currently on screen: confirm "
	        "true to accept, false to cancel.",
	        kAnswerDialogSchema);
}

void AppendLanguageActions(ActionSet &set) {
	set.add("confirm_language",
	        "Accept the languages the game is offering and go on to the title "
	        "screen.",
	        kConfirmLanguageSchema);
}

bool TrySendLanguageNotice() {
	if (g_LanguageNoticeSent.load(std::memory_order_acquire)) {
		return true;
	}
	// Not silent: this one is a question the game is holding still for, and
	// nothing else can happen until it is answered.
	if (NeuroSendContext(
	        "The game is asking which languages to use. It only asks this once, "
	        "on the very first launch, and it will not go any further until the "
	        "question is answered. The languages already selected are the ones "
	        "it will use; confirming them is what gets past this screen.",
	        false)) {
		g_LanguageNoticeSent.store(true, std::memory_order_release);
		return true;
	}
	return false;
}

void ResetLanguageNotice() {
	g_LanguageNoticeSent.store(false, std::memory_order_release);
}

}  // namespace ScreenMenu
