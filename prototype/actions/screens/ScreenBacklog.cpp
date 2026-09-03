#include "ScreenBacklog.h"

#include <windows.h>

#include <string>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "NeuroClient.h"

namespace {

// Which scroll controls are usable, and which of the two views is showing. The
// screen has one toggle for both views, so only the direction that would change
// something is offered.
constexpr int kFlagPageUp = 1 << 0;
constexpr int kFlagPageDown = 1 << 1;
constexpr int kFlagTop = 1 << 2;
constexpr int kFlagEnd = 1 << 3;
constexpr int kFlagSceneList = 1 << 4;
// The five per-row slots, as three masks above the view flags. They are part of
// the value so that scrolling -- which changes which lines are on screen --
// re-registers and re-describes the numbering.
constexpr int kJumpShift = 5;
constexpr int kVoiceShift = 10;
constexpr int kSaveShift = 15;
constexpr int kSlotMask = 0x1f;
constexpr int kRows = 5;

// How many lines of history one read returns.
constexpr int kHistoryWindow = 4;

// The two per-line controls. Their numbering is over the lines currently on
// screen, so the slot is re-checked against what is visible at this moment.
void HandleBacklogLineAction(std::string const &id,
                             std::string const &data,
                             char const *action,
                             char const *verb,
                             // 0 jump, 1 replay, 2 keep in the collection
                             int which,
                             char const *okMessage) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"line\"", number) || number < 1 || number > 5) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " requires a \"line\" between 1 and 5.")
		        .c_str());
		return;
	}
	int const slot = number - 1;

	std::string const tag(action);
	if (!ProbeStateIs("backlog", (tag + " state").c_str())) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works while the backlog is open.");
		return;
	}
	BacklogLineButtons buttons;
	if (!ProbeBacklogLineButtons(buttons)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Cannot tell which lines are on screen right now.");
		return;
	}
	int const mask = which == 0   ? buttons.jumpMask
	                 : which == 1 ? buttons.voiceMask
	                              : buttons.saveMask;
	if ((mask & (1 << slot)) == 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      which == 0 ? "That line cannot be jumped to."
		                                 : "That line has no voice.");
		return;
	}
	// Hover first, the way the row is reached with a cursor: the preview is the
	// only thing on screen that says which line this is, and the confirmation
	// that follows does not name it.
	if (which == 0) {
		DispatchBacklogLineButton("showJumpView", slot, (tag + " hover").c_str());
		Sleep(kHoverPreviewMs);
	}
	if (!DispatchBacklogLineButton(verb, slot, (tag + " dispatch").c_str())) {
		// Leaving a preview up over a press that never happened would be a lie
		// about what is selected.
		if (which == 0) {
			DispatchBacklogLineButton("hideJumpView", slot,
			                          (tag + " unhover").c_str());
		}
		NeuroSendActionResult(id.c_str(), false, "That line did not respond.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, okMessage);
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
		std::string const row =
		    window.substr(start, lineEnd == std::string::npos ? std::string::npos
		                                                      : lineEnd - start);
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

// The numbers of the rows a mask marks, as "1, 3, 4".
std::string RowList(int mask) {
	std::string list;
	for (int slot = 0; slot < kRows; ++slot) {
		if ((mask & (1 << slot)) != 0) {
			list += (list.empty() ? "" : ", ") + std::to_string(slot + 1);
		}
	}
	return list;
}

// The bound on `from` is the length of the history, and the schema is the only
// place the server can learn it -- otherwise a randomised value gets refused by
// the handler instead of never being sent.
std::string ReadBacklogSchema(int historyTotal) {
	return std::string(
	           "{\"type\":\"object\",\"properties\":{\"from\":"
	           "{\"type\":\"integer\",\"minimum\":1,"
	           "\"maximum\":") +
	       std::to_string(historyTotal) +
	       "},\"language\":{\"type\":\"string\","
	       "\"enum\":[\"displayed\",\"japanese\"]}},"
	       "\"additionalProperties\":false}";
}

}  // namespace

namespace ScreenBacklog {

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	if (name == "backlog_page_up") {
		HandleScreenButtonAction(id, data, "backlog_page_up", "backlog", "pageup",
		                         "Scrolled the backlog up.",
		                         "The backlog is already at the top.");
	} else if (name == "backlog_page_down") {
		HandleScreenButtonAction(id, data, "backlog_page_down", "backlog",
		                         "pagedown", "Scrolled the backlog down.",
		                         "The backlog is already at the bottom.");
	} else if (name == "backlog_top") {
		HandleScreenButtonAction(id, data, "backlog_top", "backlog", "top",
		                         "Jumped back in the backlog.",
		                         "There is nothing further back.");
	} else if (name == "backlog_end") {
		HandleScreenButtonAction(id, data, "backlog_end", "backlog", "end",
		                         "Jumped forward in the backlog.",
		                         "There is nothing further forward.");
	} else if (name == "show_scene_list") {
		HandleScreenButtonAction(id, data, "show_scene_list", "backlog", "modechg",
		                         "Switched to the scene list.",
		                         "Cannot switch the backlog view.");
	} else if (name == "show_backlog_lines") {
		HandleScreenButtonAction(id, data, "show_backlog_lines", "backlog",
		                         "modechg", "Switched back to the lines.",
		                         "Cannot switch the backlog view.");
	} else if (name == "jump_to_line") {
		// Deliberately not "that line": the same control lists whole scenes in
		// scene mode, and the result would then name the wrong thing.
		HandleBacklogLineAction(id, data, "jump_to_line", "jump", 0,
		                        "Asked to go back there; confirm or cancel the "
		                        "dialog.");
	} else if (name == "save_backlog_voice") {
		HandleBacklogLineAction(id, data, "save_backlog_voice", "vsave", 2,
		                        "Opened the voice collection to keep that line.");
	} else if (name == "replay_backlog_voice") {
		HandleBacklogLineAction(id, data, "replay_backlog_voice", "vreplay", 1,
		                        "Replaying that line.");
	} else if (name == "read_backlog") {
		HandleReadBacklogAction(id, data);
	} else if (name == "open_backlog") {
		HandleOpenBacklogAction(id, data);
	} else {
		return false;
	}
	return true;
}

void SendEnteringContext() {
	int total = 0;
	if (ProbeHistoryCount(total) && total > 0) {
		NeuroSendContext(("The backlog is open. It holds " + std::to_string(total) +
		                  " lines; read_backlog reads four at a time.")
		                     .c_str(),
		                 false);
	}
}

void AppendActions(ActionSet &set, int payload) {
	set.add("go_back", "Close the backlog and return to the game.",
	        kGoBackSchema);

	int historyTotal = 0;
	if (ProbeHistoryCount(historyTotal) && historyTotal > 0) {
		set.add("read_backlog",
		        set.keep("Read four lines of the conversation history, out of " +
		                 std::to_string(historyTotal) +
		                 ". Give \"from\" to start at a line number, or leave it "
		                 "out for the four most recent."),
		        set.keep(ReadBacklogSchema(historyTotal)));
	}

	if ((payload & kFlagPageUp) != 0) {
		set.add("backlog_page_up", "Scroll the backlog up one page.",
		        kBacklogPageUpSchema);
	}
	if ((payload & kFlagPageDown) != 0) {
		set.add("backlog_page_down", "Scroll the backlog down one page.",
		        kBacklogPageDownSchema);
	}
	if ((payload & kFlagTop) != 0) {
		set.add("backlog_top",
		        "Jump back to the previous scene break in the backlog, or to its "
		        "start.",
		        kBacklogTopSchema);
	}
	if ((payload & kFlagEnd) != 0) {
		set.add("backlog_end",
		        "Jump forward to the next scene break in the backlog, or to its "
		        "end.",
		        kBacklogEndSchema);
	}
	bool const sceneList = (payload & kFlagSceneList) != 0;
	if (sceneList) {
		set.add("show_backlog_lines",
		        "Switch the backlog back to the list of lines.",
		        kShowBacklogLinesSchema);
	} else {
		set.add("show_scene_list", "Switch the backlog to the scene list.",
		        kShowSceneListSchema);
	}

	// The numbering is window-relative, so it is spelled out in the description
	// each time rather than left for Neuro to infer: after a scroll the same
	// number is a different line.
	int const jumpMask = (payload >> kJumpShift) & kSlotMask;
	int const voiceMask = (payload >> kVoiceShift) & kSlotMask;
	int const saveMask = (payload >> kSaveShift) & kSlotMask;
	// The window shows four or five rows depending on where it sits, so the
	// range stops at the highest row that is actually there.
	int highest = 0;
	for (int slot = 0; slot < kRows; ++slot) {
		if (((jumpMask | voiceMask | saveMask) & (1 << slot)) != 0) {
			highest = slot + 1;
		}
	}

	std::string const jumpList = RowList(jumpMask);
	if (!jumpList.empty()) {
		// In scene mode the same control jumps to whole scenes, so the wording
		// follows the list that is actually showing.
		set.add("jump_to_line",
		        set.keep(std::string("Go back to one of the ") +
		                 (sceneList ? "scenes" : "lines") +
		                 " on screen, numbered from the top. Available now: " +
		                 jumpList + ". Asks for confirmation first."),
		        set.keep(BoundedIntSchema("line", highest)));
	}
	std::string const saveList = RowList(saveMask);
	if (!saveList.empty()) {
		set.add("save_backlog_voice",
		        set.keep("Keep the voice of one of the lines on screen in the "
		                 "collection, numbered from the top. Available now: " +
		                 saveList + "."),
		        set.keep(BoundedIntSchema("line", highest)));
	}
	std::string const voiceList = RowList(voiceMask);
	if (!voiceList.empty()) {
		set.add("replay_backlog_voice",
		        set.keep("Replay the voice of one of the lines on screen, "
		                 "numbered from the top. Available now: " +
		                 voiceList + "."),
		        set.keep(BoundedIntSchema("line", highest)));
	}
}

bool ReadPayload(int &payload) {
	int flags = 0;
	BacklogLineButtons buttons;
	if (!ProbeBacklogButtons(flags) || !ProbeBacklogLineButtons(buttons)) {
		return false;
	}
	payload = flags | (buttons.jumpMask << kJumpShift) |
	          (buttons.voiceMask << kVoiceShift) |
	          (buttons.saveMask << kSaveShift);
	return true;
}

}  // namespace ScreenBacklog
