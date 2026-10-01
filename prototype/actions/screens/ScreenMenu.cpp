#include "ScreenMenu.h"

#include "ScreenPlay.h"

#include <windows.h>

#include <atomic>
#include <string>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "ActionSet.h"
#include "NeuroClient.h"
#include "ScreenNotebook.h"

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

// The settings screen's own seventh tab is the key reference: where a player
// goes to be reminded of controls they use rarely and cannot see. This is the
// same errand for actions -- the quick menu's buttons are not registered while
// the bar is retracted, so without asking there is no way to know what is on
// it.
//
// The list goes out as context rather than as the result message: the spec asks
// that a result be small, and this is a page, not a sentence.
void HandleReadShortcutsAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "read_shortcuts does not accept arguments.");
		return;
	}
	std::string reference = ScreenPlay::QuickMenuReference();
	std::string const notebook = ScreenNotebook::ReferenceLine();
	if (!reference.empty() && !notebook.empty()) {
		reference += "\n" + notebook;
	}
	if (reference.empty()) {
		NeuroSendActionResult(id.c_str(), false,
		                      "There is nothing to list for the quick menu.");
		return;
	}
	// What used to be the second half of the action's description. It belongs
	// here rather than there: a description is read while the action is on
	// offer and goes with the screen, while this stays in context, and it is
	// the part that makes the list worth having.
	NeuroSendContext(("The quick menu carries these actions:\n" + reference +
	                  "\nThey are not offered while the bar is retracted, but "
	                  "they still work then, the way a player's shortcut keys "
	                  "do.")
	                     .c_str(),
	                 false);
	NeuroSendActionResult(id.c_str(), true, "Listed the quick menu's actions.");
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
	// The game asks for confirmation when SystemConfig.askTitle is set, so this
	// action deliberately stops at the question rather than answering it: losing
	// unsaved progress should not happen from one call. answer_dialog becomes
	// available as soon as the dialog is up.
	//
	// With the question switched off in the game's settings there is no dialog,
	// and the answer says so instead of sending her to confirm nothing.
	bool asked = true;
	if (ok) {
		Sleep(kDialogSettleMs);
		asked = ProbeDialogVisible("return_to_title asked");
	}
	AnswerPress(
	    id, ok,
	    asked
	        ? "Asked to return to the title screen; confirm or cancel the dialog."
	        : "Going back to the title screen.",
	    "Returning to the title screen is unavailable.");
}

// What each confirmation is about, as the outcome of answering it rather than
// as the question. Used for the result of answer_dialog: "Confirmed the dialog"
// leaves the caller to work out what was just agreed to from the screen that
// follows, which for an erase means reading a slot list to notice a save is
// gone. `nullptr` for a question with no wording of its own.
char const *ConfirmedOutcome(std::string const &part) {
	if (part == "AskDelete") {
		return "Confirmed: the save was erased.";
	}
	if (part == "AskLoad" || part == "AskQLoad") {
		return "Confirmed: loading that save.";
	}
	if (part == "AskSave" || part == "AskQSave") {
		return "Confirmed: writing that save.";
	}
	if (part == "AskOverwrite") {
		return "Confirmed: that slot was overwritten.";
	}
	if (part == "AskCopy") {
		return "Confirmed: the save was copied into that slot.";
	}
	if (part == "AskMove") {
		return "Confirmed: the save was moved into that slot.";
	}
	if (part == "AskTitle") {
		return "Confirmed: going back to the title screen.";
	}
	if (part == "AskExit") {
		return "Confirmed: the game is quitting.";
	}
	if (part == "AskInit") {
		return "Confirmed: the settings were reset to defaults.";
	}
	return nullptr;
}

// Whether agreeing to this question puts the story somewhere else.
bool IsJumpQuestion(std::string const &part) {
	return part == "AskLoad" || part == "AskQLoad" || part == "AskJump" ||
	       part == "AskNext" || part == "AskBackTo" || part == "AskPrevScn" ||
	       part == "AskNextScn";
}

void HandleAnswerDialogAction(std::string const &id, std::string const &data) {
	int confirmValue = 0;
	if (!ParseJsonBoolean(data, "\"confirm\"", confirmValue)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "answer_dialog requires a boolean \"confirm\".");
		return;
	}
	bool const confirm = confirmValue != 0;

	// Read before the press, not after: answering closes the dialog, and with it
	// goes the only thing that says what was being asked.
	std::string part;
	if (!confirm || !ProbeDialogTextPart(part)) {
		part.clear();
	}
	// The same for where a load goes: the slot is named on the screen being
	// answered, and that screen is gone once the answer lands.
	std::string loadTitle;
	if ((part == "AskLoad" && !ProbeLoadSelectionTitle(loadTitle)) ||
	    (part == "AskQLoad" && !ProbeQuickLoadTitle(loadTitle))) {
		loadTitle.clear();
	}

	bool const ok = ProbeDialogLinkReady(confirm, "answer_dialog ready") &&
	                DispatchDialogLink(confirm, "answer_dialog dispatch");
	if (!ok) {
		// A failure is a request to try again: the specification has Neuro retry
		// a forced action that failed, and the server puts the force back in its
		// queue. That is right while the dialog stands and something went wrong
		// with the press, and wrong once the dialog is gone -- retrying then can
		// never succeed, and the pair loops. So a dialog that is no longer there
		// is reported as nothing left to do rather than as a failure.
		if (!ProbeDialogVisible("answer_dialog gone")) {
			NeuroSendActionResult(
			    id.c_str(), true,
			    "The dialog is already closed; there is nothing to answer.");
			return;
		}
		NeuroSendActionResult(
		    id.c_str(), false,
		    LastPressWasBusy()
		        ? kPressStillRunning
		        : "The dialog is up but its buttons did not answer.");
		return;
	}
	// The story is about to stand somewhere else. Said here because a jump
	// forward inside a scene leaves no other trace: the position only moves on,
	// as it does when reading.
	if (IsJumpQuestion(part)) {
		ScreenNotebook::NoteJump();
	}
	if (!loadTitle.empty()) {
		NeuroSendActionResult(
		    id.c_str(), true,
		    ("Confirmed: loading that save, \"" + loadTitle + "\".").c_str());
		return;
	}
	char const *const outcome = part.empty() ? nullptr : ConfirmedOutcome(part);
	NeuroSendActionResult(id.c_str(), true,
	                      outcome   ? outcome
	                      : confirm ? "Confirmed the dialog."
	                                : "Cancelled the dialog.");
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
	AnswerPress(id, ok, "Returned to the previous screen.",
	            "Going back is unavailable.");
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
	AnswerPress(id, ok,
	            "Language confirmed. The game is going on to the title "
	            "screen.",
	            "The language screen is no longer up.");
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
	} else if (name == "read_shortcuts") {
		HandleReadShortcutsAction(id, data);
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
		        "Leave the game and go back to the title screen. May ask for "
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
	// Offered alongside the tabs, because it answers the same need the seventh
	// of them does: a reminder of controls that are not in front of you.
	set.add("read_shortcuts",
	        "List the quick menu's actions and what each one does.",
	        kReadShortcutsSchema);
}

void AppendDialogActions(ActionSet &set) {
	set.add("answer_dialog",
	        "Answer the dialog on screen: confirm true to accept, false to "
	        "cancel.",
	        kAnswerDialogSchema);
}

bool DialogAsksForJump() {
	std::string part;
	return ProbeDialogTextPart(part) && IsJumpQuestion(part);
}

void ForceDialogAnswer() {
	std::string part;
	char const *subject = "The game is asking for a confirmation.";
	// The tags come from MyYesNoDialog's own yesnoMap, which keys the dialog's
	// Japanese question text to these names; modules may add their own, as the
	// backlog does with AskJump.
	if (ProbeDialogTextPart(part)) {
		if (part == "AskTitle") {
			subject = "The game is asking whether to return to the title screen.";
		} else if (part == "AskExit") {
			subject = "The game is asking whether to quit.";
		} else if (part == "AskLoad") {
			subject = "The game is asking whether to load that save.";
		} else if (part == "AskQLoad") {
			subject = "The game is asking whether to load the quick save.";
		} else if (part == "AskQSave") {
			subject = "The game is asking whether to write the quick save.";
		} else if (part == "AskOverwrite") {
			subject = "The game is asking whether to overwrite that slot.";
		} else if (part == "AskNext") {
			subject = "The game is asking whether to go on to the next choice.";
		} else if (part == "AskBackTo") {
			subject = "The game is asking whether to go back to the previous choice.";
		} else if (part == "AskJump") {
			subject = "The game is asking whether to jump back to that line.";
		} else if (part == "AskInit") {
			subject = "The game is asking whether to reset it to defaults.";
		} else if (part == "AskSave") {
			subject = "The game is asking whether to write that save.";
		} else if (part == "AskDelete") {
			subject = "The game is asking whether to erase that save.";
		} else if (part == "AskCopy") {
			subject =
			    "The game is asking whether to copy that save into the slot "
			    "chosen.";
		} else if (part == "AskMove") {
			subject =
			    "The game is asking whether to move that save into the slot "
			    "chosen.";
		} else if (part == "AskAllRemove1" || part == "AskAllRemove2") {
			// Two questions in a row for the same thing, the second one a second
			// chance to back out. Nothing we register raises them; they are named
			// so that a stray one does not reach Neuro as an unlabelled question.
			subject =
			    "The game is asking whether to erase every save. It asks "
			    "this twice.";
		} else if (part == "AskPrevScn") {
			subject = "The game is asking whether to go back to the previous scene.";
		} else if (part == "AskNextScn") {
			subject = "The game is asking whether to go on to the next scene.";
		}
	}
	char const *names[] = {"answer_dialog"};
	// Ephemeral: the question holds only while the dialog stands, and it is
	// answered within moments either way.
	NeuroForceAction(subject, "Answer it: confirm to accept, cancel to decline.",
	                 names, 1, true);
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

// The screen is modal in the same sense the confirmation dialog is: the game
// holds still until it is answered, and nothing else is on offer. So the force
// goes out through the same path as the other modals, and is counted the same
// way -- one per appearance, retried until it lands, forgotten with the
// session.
//
// It still waits on the notice above rather than on entering the state, and
// that ordering is the reason this is a function of its own: the screen is up
// while the SDK may still be connecting, and a force sent then is dropped. The
// notice is what retries until the connection exists; until it does, the action
// is simply registered and works as an ordinary one.
void ForceLanguageConfirm() {
	char const *names[] = {"confirm_language"};
	// Ephemeral: the notice carries the explanation into context already, and
	// repeating it there would say the same thing twice.
	NeuroForceAction("The game is waiting for the languages to be confirmed.",
	                 "Confirm them to get past this screen.", names, 1, true);
}

void ResetLanguageNotice() {
	g_LanguageNoticeSent.store(false, std::memory_order_release);
}

}  // namespace ScreenMenu
