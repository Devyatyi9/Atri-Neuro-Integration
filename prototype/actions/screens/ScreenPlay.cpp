#include "ScreenPlay.h"

#include <windows.h>

#include <cstddef>
#include <string>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "ActionSet.h"
#include "NeuroClient.h"

namespace {

// Play is not one state but a screen carrying a few independent booleans: a
// choice is up, the quick menu bar is out, auto runs, skipping runs.
// Enumerating the combinations would mean a constant per pair, so it is a base
// plus flags; the value only has to be distinct per action set.
//
// What the bar's buttons currently allow is deliberately not among them. Every
// button on the bar is on screen at all times -- the game greys the ones that
// do not apply -- so they are offered whenever the bar is out. Folding
// availability in here instead changed the set on almost every line of
// dialogue, as a voice or a scene break came and went.
//
// Choice versus text is a disposable action window in the sense of the SDK's
// own guidance: while the select layer is up, advancing does nothing, and
// select_choice stops being answerable the moment the layer closes.
constexpr int kFlagChoice = 1 << 0;
constexpr int kFlagPanelOut = 1 << 1;
constexpr int kFlagAuto = 1 << 2;
constexpr int kFlagSkip = 1 << 3;

void HandleAdvanceTextAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "advance_text does not accept arguments.");
		return;
	}

	switch (AdvanceDialogue("advance")) {
		case AdvanceResult::Advanced:
			NeuroSendActionResult(id.c_str(), true, "Text advanced.");
			return;
		case AdvanceResult::MoviePlaying:
			// Named rather than folded into the general refusal: a movie ends on
			// its own, and knowing that is the difference between waiting and
			// retrying something that cannot work yet.
			NeuroSendActionResult(id.c_str(), false,
			                      "A movie is playing; the text will continue "
			                      "when it ends.");
			return;
		default:
			NeuroSendActionResult(id.c_str(), false, "Text advance is unavailable.");
			return;
	}
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

	bool const pressed = ProbeStateIs("game", "quick_menu state") &&
	                     ProbeQuickMenuCanDropDown("quick_menu dispatchable") &&
	                     DispatchQuickMenuOpen("quick_menu dispatch");
	// Pressed is not arrived. In a scene entered through the engine's own start
	// point the layer was never built, and it takes the press without anything
	// reaching the screen -- so the bar is looked at afterwards, the way the
	// player would look at it, and a press that changed nothing is reported as
	// the failure it is rather than as an opening nobody can see.
	if (!pressed || !ProbeQuickMenuCameOut("quick_menu arrived")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the quick menu is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the quick menu.");
}

void HandleHideUiAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "hide_ui does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "hide_ui state") &&
	                ProbeQuickMenuButtonReady("hide", "hide_ui button") &&
	                DispatchQuickMenuButton("hide", "hide_ui dispatch");
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

void HandleShowUiAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "show_ui does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "show_ui state") &&
	                DispatchShowTextWindow("show_ui dispatch");
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, "The text window is not hidden.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Brought the text window back.");
}

// auto and skip are the same bar button pressed twice: one press turns the
// mode on, the next turns it off, and the button stays enabled throughout.
// Which direction is registered comes from the mode flag, so the handler only
// has to press.
// The four strings that make one of these travel together rather than as
// loose arguments: six const char* in a row is the shape where two get
// swapped, and a swapped pair here answers a working action with the wrong
// sentence.
struct QuickMenuAction {
	char const *name;
	char const *button;
	char const *ok;
	char const *fail;
};

void HandleQuickMenuToggle(std::string const &id,
                           std::string const &data,
                           QuickMenuAction const &action) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action.name) + " does not accept arguments.").c_str());
		return;
	}

	std::string const tag(action.name);
	bool const ok =
	    ProbeStateIs("game", (tag + " state").c_str()) &&
	    ProbeQuickMenuButtonReady(action.button, (tag + " button").c_str()) &&
	    DispatchQuickMenuButton(action.button, (tag + " dispatch").c_str());
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, action.fail);
		return;
	}
	NeuroSendActionResult(id.c_str(), true, action.ok);
}

// Stepping between choice points asks first (asknextscn / askprevscn), the way
// returning to the title does -- so the result says the question was raised,
// not that the jump happened. The confirmation is configurable, so whether it
// actually appeared is read back rather than assumed.
struct SceneStepAction {
	char const *name;
	char const *button;
	char const *asked;
	char const *done;
	char const *fail;
};

void HandleSceneStepAction(std::string const &id,
                           std::string const &data,
                           SceneStepAction const &action) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action.name) + " does not accept arguments.").c_str());
		return;
	}

	std::string const tag(action.name);
	bool const ok =
	    ProbeStateIs("game", (tag + " state").c_str()) &&
	    ProbeQuickMenuButtonReady(action.button, (tag + " button").c_str()) &&
	    DispatchQuickMenuButton(action.button, (tag + " dispatch").c_str());
	if (!ok) {
		NeuroSendActionResult(id.c_str(), false, action.fail);
		return;
	}
	Sleep(kDialogSettleMs);
	NeuroSendActionResult(id.c_str(), true,
	                      ProbeDialogVisible((tag + " asked").c_str())
	                          ? action.asked
	                          : action.done);
}

// Builds the schema for a single integer field bounded to what is actually on
// screen right now. The bound has to travel with the schema rather than being
// baked in: the gallery's last page carries fewer squares, and the backlog
// shows fewer rows near the ends of the log.
void HandleReadCurrentLineAction(std::string const &id,
                                 std::string const &data) {
	std::string line;
	if (!ProbeCurrentLine(ParseReadLanguage(data), line)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "There is no line on screen to read.");
		return;
	}
	size_t const tab = line.find('\t');
	std::string message = "Current line: ";
	if (tab != std::string::npos && tab > 0) {
		message += line.substr(0, tab) + ": ";
	}
	message += tab == std::string::npos ? line : line.substr(tab + 1);
	NeuroSendActionResult(id.c_str(), true, message.c_str());
}

}  // namespace

namespace ScreenPlay {

bool IsTextHidden(ScreenSnapshot const &screen) {
	return screen.textWindowHidden;
}

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	if (name == "advance_text") {
		HandleAdvanceTextAction(id, data);
	} else if (name == "select_choice") {
		HandleSelectChoiceAction(id, data);
	} else if (name == "read_current_line") {
		HandleReadCurrentLineAction(id, data);
	} else if (name == "open_quick_menu") {
		HandleOpenQuickMenuAction(id, data);
	} else if (name == "close_quick_menu") {
		HandleCloseQuickMenuAction(id, data);
	} else if (name == "hide_ui") {
		HandleHideUiAction(id, data);
	} else if (name == "show_ui") {
		HandleShowUiAction(id, data);
	} else if (name == "start_auto_mode") {
		HandleQuickMenuToggle(id, data,
		                      {"start_auto_mode", "auto",
		                       "Auto mode is on; the text advances on its own.",
		                       "Turning auto mode on is unavailable."});
	} else if (name == "stop_auto_mode") {
		HandleQuickMenuToggle(id, data,
		                      {"stop_auto_mode", "auto", "Auto mode is off.",
		                       "Turning auto mode off is unavailable."});
	} else if (name == "start_skip_mode") {
		HandleQuickMenuToggle(
		    id, data,
		    {"start_skip_mode", "skip", "Skipping through the text.",
		     "Skip is greyed out: the game only skips text that has already "
		     "been read."});
	} else if (name == "stop_skip_mode") {
		HandleQuickMenuToggle(id, data,
		                      {"stop_skip_mode", "skip", "Stopped skipping.",
		                       "Turning skip mode off is unavailable."});
	} else if (name == "quick_save") {
		HandleQuickMenuToggle(id, data,
		                      {"quick_save", "qsave", "Saved to the quick slot.",
		                       "Quick save is greyed out right now."});
	} else if (name == "quick_load") {
		HandleQuickMenuToggle(id, data,
		                      {"quick_load", "qload", "Loading the quick save.",
		                       "There is no quick save to load."});
	} else if (name == "save_voice_line") {
		HandleQuickMenuToggle(
		    id, data,
		    {"save_voice_line", "vsave",
		     "Opened the voice collection to keep this line.",
		     "The keep button is greyed out: this line has no voice."});
	} else if (name == "replay_voice") {
		HandleQuickMenuToggle(
		    id, data,
		    {"replay_voice", "vreplay", "Playing that line again.",
		     "There is no voice line to replay."});
	} else if (name == "open_save") {
		HandleQuickMenuToggle(id, data,
		                      {"open_save", "save", "Opened the save screen.",
		                       "Opening the save screen is unavailable."});
	} else if (name == "previous_scene") {
		HandleSceneStepAction(id, data,
		                      {"previous_scene", "xprev",
		                       "Asked to go back to the previous choice point; "
		                       "confirm or cancel the dialog.",
		                       "Went back to the previous choice point.",
		                       "There is nothing to go back to."});
	} else if (name == "next_scene") {
		HandleSceneStepAction(id, data,
		                      {"next_scene", "next",
		                       "Asked to jump ahead to the next choice point; "
		                       "confirm or cancel the dialog.",
		                       "Jumped ahead to the next choice point.",
		                       "There is nothing to jump ahead to."});
	} else {
		return false;
	}
	return true;
}

// Defined below, next to the reference that is built from it.
void AppendQuickMenuActions(ActionSet &set, int payload);

void AppendActions(ActionSet &set, int payload) {
	// Reading the line on screen is not a quick menu affair -- it is about the
	// dialogue itself, and it is the only way to get the original Japanese,
	// which the context feed never carries.
	set.add("read_current_line",
	        "Read the line currently on screen. Pass language \"japanese\" for "
	        "the original wording.",
	        kReadLineSchema);
	if ((payload & kFlagChoice) != 0) {
		set.add("select_choice",
		        "Choose a dialogue option by its number in the list, counting "
		        "from 1.",
		        kSelectChoiceSchema);
	} else {
		set.add("advance_text", "Advance the current dialogue.",
		        kAdvanceTextSchema);
	}

	AppendQuickMenuActions(set, payload);
}

// The quick menu is reachable during a choice too, so it is offered in every
// play state -- only its direction follows the bar's position.
//
// What is registered still mirrors the bar: with it retracted, its buttons are
// not on screen and are not offered. Reaching them is a separate matter -- the
// handlers no longer require the bar to be out, so a call that arrives for a
// button the bar is currently hiding is carried out rather than refused.
//
// Kept apart from the rest of the play set so the settings screen's reference
// can be built from this same list: a reference assembled by hand would drift
// from the bar the first time a button changed.
void AppendQuickMenuActions(ActionSet &set, int payload) {
	if ((payload & kFlagPanelOut) == 0) {
		set.add("open_quick_menu",
		        "Slide out the quick menu bar so its buttons are visible.",
		        kOpenQuickMenuSchema);
		return;
	}

	// The bar itself first, then its buttons in the order they sit on it: three
	// icons across the top, then five rows of two. A set that mirrors the panel
	// reads like the panel.
	set.add("close_quick_menu", "Retract the quick menu bar.",
	        kCloseQuickMenuSchema);
	set.add("hide_ui", "Hide the text window so only the artwork is on screen.",
	        kHideUiSchema);
	// Offered whether or not the game will take them right now, because that is
	// what the bar shows: every button is on it, greyed when it does not apply.
	// Pressing a greyed one is answered with what is missing.
	set.add("replay_voice", "Play the current line's voice again.",
	        kReplayVoiceSchema);
	set.add("save_voice_line",
	        "Add the current line's voice to the voice collection, on the Extra "
	        "screen.",
	        kSaveVoiceSchema);
	set.add("open_save", "Open the save screen to store progress in a slot.",
	        kOpenSaveSchema);
	set.add("open_load", "Open the load screen to pick a saved game.",
	        kOpenLoadSchema);
	set.add("quick_save",
	        "Save straight into the quick slot, without opening the save screen.",
	        kQuickSaveSchema);
	set.add("quick_load",
	        "Load the most recent quick save. Asks for confirmation first.",
	        kQuickLoadSchema);
	set.add("previous_scene",
	        "Go back to the previous choice point. Asks for confirmation first.",
	        kPreviousSceneSchema);
	set.add("next_scene",
	        "Jump ahead to the next choice point, the same place skipping stops "
	        "at. Asks for confirmation first.",
	        kNextSceneSchema);
	// Auto and skip are each one bar button pressed twice, so only the direction
	// that would change something is offered.
	if ((payload & kFlagAuto) != 0) {
		set.add("stop_auto_mode",
		        "Turn off auto mode so the text stops advancing on its own.",
		        kStopAutoModeSchema);
	} else {
		set.add("start_auto_mode",
		        "Turn on auto mode so the text advances on its own.",
		        kStartAutoModeSchema);
	}
	if ((payload & kFlagSkip) != 0) {
		set.add("stop_skip_mode", "Stop skipping and read normally again.",
		        kStopSkipModeSchema);
	} else {
		set.add("start_skip_mode",
		        "Skip quickly through the text until it is turned off or a choice "
		        "appears.",
		        kStartSkipModeSchema);
	}
	set.add("open_backlog", "Open the backlog to re-read earlier lines.",
	        kOpenBacklogSchema);
	set.add("open_settings", "Open the settings screen.", kOpenSettingsSchema);
}

std::string QuickMenuReference() {
	// Built with the bar treated as out and usable, because the question the
	// reference answers is "what is on the bar", not "what can be pressed from
	// where I am standing". Auto and skip each occupy one button that changes
	// direction, so only one of each pair is listed -- which is what the bar
	// itself shows.
	ActionSet set;
	AppendQuickMenuActions(set, kFlagPanelOut);

	// One per line. Joined by spaces this is a paragraph of fourteen entries
	// whose boundaries are marked by nothing, and a reader that runs past the
	// end of one lands mid-sentence in the next.
	std::string reference;
	for (int index = 0; index < set.count(); ++index) {
		NeuroRegisteredAction const &action = set.data()[index];
		if (!reference.empty()) {
			reference += "\n";
		}
		reference += std::string(action.name) + " -- " + action.description;
	}
	return reference;
}

void AppendHiddenActions(ActionSet &set) {
	set.add("show_ui",
	        "Bring the dialogue interface back so reading can continue.",
	        kShowUiSchema);
}

int ReadPayload(ScreenSnapshot const &screen) {
	int payload = 0;
	if (screen.choiceVisible) {
		payload |= kFlagChoice;
	}
	// Out means out where the player can see it. A layer that was never built
	// still reports itself dropped down once pressed, and taking that reading
	// alone put every button of the bar on offer with nothing on screen to
	// press. Opening it is offered either way, and answers for itself.
	if (screen.quickMenuOut && screen.quickMenuVisible) {
		payload |= kFlagPanelOut;
		// The modes live on the bar, so they only change the action set while it
		// is out. Folding them in unconditionally would re-register an identical
		// set every time auto or skip flipped behind a retracted bar.
		if (screen.autoMode)
			payload |= kFlagAuto;
		if (screen.skipMode)
			payload |= kFlagSkip;
	}
	return payload;
}

bool HasChoice(int payload) {
	return (payload & kFlagChoice) != 0;
}

bool HasPanel(int payload) {
	return (payload & kFlagPanelOut) != 0;
}

}  // namespace ScreenPlay
