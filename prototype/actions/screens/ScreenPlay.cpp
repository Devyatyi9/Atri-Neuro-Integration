#include "ScreenPlay.h"

#include <windows.h>

#include <string>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "Logging.h"
#include "NeuroClient.h"

namespace {

// Play is not one state but a screen carrying several independent booleans: a
// choice is up, the quick menu bar is out, auto runs, skipping runs, and the
// bar's own buttons come and go with what the game allows. Enumerating the
// combinations would mean a constant per pair, so it is a base plus flags; the
// value only has to be distinct per action set.
//
// Choice versus text is a disposable action window in the sense of the SDK's
// own guidance: while the select layer is up, advancing does nothing, and
// select_choice stops being answerable the moment the layer closes.
constexpr int kFlagChoice = 1 << 0;
constexpr int kFlagPanelOut = 1 << 1;
constexpr int kFlagAuto = 1 << 2;
constexpr int kFlagSkip = 1 << 3;
constexpr int kFlagVoice = 1 << 4;
constexpr int kFlagPrevious = 1 << 5;
constexpr int kFlagNext = 1 << 6;
constexpr int kFlagQuickSave = 1 << 7;
constexpr int kFlagQuickLoad = 1 << 8;
constexpr int kFlagSaveVoice = 1 << 9;

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

void HandleHideUiAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "hide_ui does not accept arguments.");
		return;
	}

	bool const ok = ProbeStateIs("game", "hide_ui state") &&
	                ProbeQuickMenuIsOut("hide_ui dropdown") &&
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
	bool const ok =
	    ProbeStateIs("game", (tag + " state").c_str()) &&
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
	bool const ok =
	    ProbeStateIs("game", (tag + " state").c_str()) &&
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
	} else if (name == "quick_save") {
		HandleQuickMenuToggle(id, data, "quick_save", "qsave",
		                      "Saved to the quick slot.",
		                      "Quick saving is unavailable.");
	} else if (name == "quick_load") {
		HandleQuickMenuToggle(id, data, "quick_load", "qload",
		                      "Loading the quick save.",
		                      "There is no quick save to load.");
	} else if (name == "save_voice_line") {
		HandleQuickMenuToggle(id, data, "save_voice_line", "vsave",
		                      "Opened the voice collection to keep this line.",
		                      "Keeping this line is unavailable.");
	} else if (name == "replay_voice") {
		HandleQuickMenuToggle(id, data, "replay_voice", "vreplay",
		                      "Playing that line again.",
		                      "There is no voice line to replay.");
	} else if (name == "open_save") {
		HandleQuickMenuToggle(id, data, "open_save", "save",
		                      "Opened the save screen.",
		                      "Opening the save screen is unavailable.");
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
	} else {
		return false;
	}
	return true;
}

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

	// The quick menu is reachable during a choice too, so it is offered in every
	// play state -- only its direction follows the bar's position.
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
	// The game already decides when these apply -- a voice to replay, a line
	// worth keeping, a scene history to step back into, a next stop to jump to --
	// and the bar shows it in the buttons. Offering them otherwise would
	// advertise a dead press.
	if ((payload & kFlagVoice) != 0) {
		set.add("replay_voice", "Play the current line's voice again.",
		        kReplayVoiceSchema);
	}
	if ((payload & kFlagSaveVoice) != 0) {
		set.add("save_voice_line",
		        "Add the current line's voice to the voice collection, on the "
		        "Extra screen.",
		        kSaveVoiceSchema);
	}
	set.add("open_save", "Open the save screen to store progress in a slot.",
	        kOpenSaveSchema);
	set.add("open_load", "Open the load screen to pick a saved game.",
	        kOpenLoadSchema);
	if ((payload & kFlagQuickSave) != 0) {
		set.add("quick_save",
		        "Save straight into the quick slot, without opening the save "
		        "screen.",
		        kQuickSaveSchema);
	}
	if ((payload & kFlagQuickLoad) != 0) {
		set.add("quick_load",
		        "Load the most recent quick save. Asks for confirmation first.",
		        kQuickLoadSchema);
	}
	if ((payload & kFlagPrevious) != 0) {
		set.add("previous_scene", "Go back to the previous scene break.",
		        kPreviousSceneSchema);
	}
	if ((payload & kFlagNext) != 0) {
		set.add("next_scene",
		        "Jump ahead to the next scene break, the same place skipping "
		        "would stop at.",
		        kNextSceneSchema);
	}
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
	if (screen.quickMenuOut) {
		payload |= kFlagPanelOut;
		// The modes live on the bar, so they only change the action set while it
		// is out. Folding them in unconditionally would re-register an identical
		// set every time auto or skip flipped behind a retracted bar.
		if (screen.autoMode)
			payload |= kFlagAuto;
		if (screen.skipMode)
			payload |= kFlagSkip;
		if (screen.canReplayVoice)
			payload |= kFlagVoice;
		if (screen.canGoPrevious)
			payload |= kFlagPrevious;
		if (screen.canGoNext)
			payload |= kFlagNext;
		if (screen.canQuickSave)
			payload |= kFlagQuickSave;
		if (screen.canQuickLoad)
			payload |= kFlagQuickLoad;
		if (screen.canSaveVoice)
			payload |= kFlagSaveVoice;
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
