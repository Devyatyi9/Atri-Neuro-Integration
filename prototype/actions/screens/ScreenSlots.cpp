#include "ScreenSlots.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "ActionSet.h"
#include "ContextPayload.h"
#include "NeuroClient.h"
#include "ParseNumber.h"
#include "SlotPayload.h"

namespace {

// The scenarios behind the three modes. Which one runs is what a slot press
// does; the tab bar switches between them.
constexpr char const *kLoadScenario = "load.ks";
constexpr char const *kSaveScenario = "save.ks";
constexpr char const *kVoiceScenario = "voice.ks";

// The screen shows ten slots in two rows of five. A third row of controls
// exists but is not part of the window, and its flags are stale, so it is never
// read. Actions number the ten by what is on screen; the list behind them is
// reached through the window base, which the page buttons move.
constexpr int kVisibleSlots = 10;
constexpr int kSlotColumns = 5;

// The tab bar, carried in the payload: which tabs the bar shows, and which of
// those can be pressed. The tab that is open has no button of its own, so
// switching tabs changes the mask and re-registers the set.
//
// How all of this sits inside the one int the screen travels as lives in
// SlotPayload, and only there.
constexpr int kSaveTabSave = kSlotTabSave;
constexpr int kSaveTabLoad = kSlotTabLoad;
constexpr int kSaveTabQuick = kSlotTabQuick;
constexpr int kSaveTabVoice = kSlotTabVoice;

// In the order the probe reports them.
char const *const kCopyModeVerbs[] = {"", "copied", "moved", "swapped with"};

// The menu's entries, in the order of their bits. `delete` is the name the
// panel gives the entry the screen labels Erase.
char const *const kEditItemControls[] = {"comment", "copy", "move", "delete"};
char const *const kEditItemActions[] = {"edit_comment", "edit_copy",
                                        "edit_move", "edit_erase"};
// What the entries are called on screen. Only the last differs from the control
// name: the panel calls it `delete` and the screen labels it Erase.
char const *const kEditItemLabels[] = {"comment", "copy", "move", "erase"};

// The four tabs, in the order of their bits, which is the order they sit on
// screen from the left.
char const *const kSaveTabNames[] = {"save", "load", "quick", "voice"};
char const *const kSaveTabHolds[] = {"writing a new save", "saved games",
                                     "quick saves", "kept voice lines"};

// The last list announced, so an identical one is not sent again.
std::string g_LastAnnouncement;

// The store holds the quick saves first and the numbered ones after them, in
// one list: ten quick slots at indices 0..9, then the fifteen pages of ten the
// screen numbers No.001 upward. So an index is nine ahead of the number printed
// on the plate, and the two are easy to mistake for each other -- the third
// page sits at indices 30..39 and shows No.021 to No.030.
//
// Every place that has an index and wants the printed number goes through here,
// so the offset is written down once.
constexpr int kQuickSlots = 10;

// The number printed under a slot. The two halves of the list are numbered
// separately on screen: the quick ones are Quick 1 to Quick 10, and the rest
// start again at No.001. Every slot the player can point at has exactly one of
// these, and it is what the actions take.
int ScreenNumber(int index) {
	return index < kQuickSlots ? index + 1 : index - kQuickSlots + 1;
}

// The same number as the screen writes it, for saying back to Neuro.
std::string PlateLabel(int index) {
	if (index < kQuickSlots) {
		return "Quick " + std::to_string(ScreenNumber(index));
	}
	char plate[16];
	sprintf_s(plate, "No.%03d", ScreenNumber(index));
	return plate;
}

bool PressSlot(int position, char const *tag) {
	char itemButton[16];
	sprintf_s(itemButton, "item%d%d", position / kSlotColumns,
	          position % kSlotColumns);
	std::string const label(tag);
	return ProbeScreenButtonReady(itemButton, (label + " button").c_str()) &&
	       DispatchScreenButton(itemButton, (label + " dispatch").c_str());
}

// Both slot actions number from 1, like the list their context message sends
// and like select_choice. kag's bookmark index is 0-based.
// Slots are numbered the way the screen numbers them: 1 to 10 over the ten
// cells on view, top left first. `position` comes back 0-based, which is what
// the item controls use.
bool ParseSlot(std::string const &id,
               std::string const &data,
               char const *action,
               int &position) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"slot\"", number)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " requires a \"slot\" number, the one printed "
		                           "under the slot on screen.")
		        .c_str());
		return false;
	}

	// Which ten are on screen decides what the number may be, so the window is
	// read rather than assumed. Numbering by the plate rather than by the cell
	// is what makes a number mean one save: cell 2 is a different save on every
	// page, while No.022 is always the same one.
	int base = -1;
	int page = -1;
	if (!ProbeSaveWindow(base, page)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "Cannot tell which slots the screen is showing right now.");
		return false;
	}
	int const first = ScreenNumber(base);
	int const last = ScreenNumber(base + kVisibleSlots - 1);
	if (number < first || number > last) {
		// Naming the page it is on rather than switching there: a page change is
		// something the player sees happen, and doing it inside another action
		// would hide it.
		std::string message = std::string(action) + " takes a slot from " +
		                      std::to_string(first) + " to " +
		                      std::to_string(last) + ", the ten on screen now.";
		if (number >= 1 && base >= kQuickSlots) {
			int const wanted = (number - 1) / kVisibleSlots + 1;
			message += " Slot " + std::to_string(number) + " is on page " +
			           std::to_string(wanted) + ". Open it with switch_page.";
		}
		NeuroSendActionResult(id.c_str(), false, message.c_str());
		return false;
	}
	position = number - first;
	return true;
}

// The index in the list behind the screen of the slot at `position`. The window
// base is the only thing that ties the two together, and it moves with the
// page.
bool SlotIndex(std::string const &id, int position, int &index) {
	int base = -1;
	int page = -1;
	if (!ProbeSaveWindow(base, page)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "Cannot tell which slots the screen is showing right now.");
		return false;
	}
	index = base + position;
	return true;
}

// The slot list gives two fields per row so that ten rows stay readable. This
// gives the rest of one slot: what the player wrote over it, and the line the
// game was on when it was written -- which together are how a person tells two
// saves of the same scene apart.
void HandleReadSaveSlotAction(std::string const &id, std::string const &data) {
	int position = -1;
	if (!ParseSlot(id, data, "read_save_slot", position)) {
		return;
	}

	std::string record;
	if (!ProbeSlotDetails(position, record)) {
		NeuroSendActionResult(id.c_str(), false, "Could not read that slot.");
		return;
	}
	if (record[0] == '0') {
		NeuroSendActionResult(id.c_str(), true, "That slot is empty.");
		return;
	}

	std::string fields[6];
	size_t start = 0;
	for (int field = 0; field < 6; ++field) {
		size_t const tab = record.find('\t', start);
		fields[field] = record.substr(
		    start, tab == std::string::npos ? std::string::npos : tab - start);
		if (tab == std::string::npos) {
			break;
		}
		start = tab + 1;
	}

	// Named the way the screen names it, which is also the way it was asked
	// for. ProbeSaveWindow rather than SlotIndex: that helper answers the action
	// itself when it fails, and this one has an answer of its own to send.
	int base = -1;
	int page = -1;
	std::string message =
	    ProbeSaveWindow(base, page) ? PlateLabel(base + position) : "That slot";
	message += ": " + fields[1] + ", saved " + fields[2] + ".";
	// The comment replaces the last line on screen once it is set, so it is
	// reported the same way round: what the player chose to see, and then what
	// it was written over.
	if (!fields[3].empty()) {
		message += " Comment: " + fields[3] + ".";
	}
	// The badge the screen shows, in words. It answers "which of these did I
	// write last", which is the question a list of dates makes you work out.
	if (fields[4] == "1") {
		message += " Marked New: the most recent save.";
	}
	if (!fields[5].empty()) {
		message += " Last line: " + fields[5];
	}
	NeuroSendActionResult(id.c_str(), true, message.c_str());
}

void HandleLoadSaveAction(std::string const &id, std::string const &data) {
	if (!ProbeScenarioIs(kLoadScenario, "load_save screen")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the load screen.");
		return;
	}

	int position = -1;
	if (!ParseSlot(id, data, "load_save", position)) {
		return;
	}

	int index = -1;
	if (!SlotIndex(id, position, index)) {
		return;
	}
	if (!ProbeBookmarkExists(index, "load_save exists")) {
		NeuroSendActionResult(id.c_str(), false, "That slot is empty.");
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
	    "Selected the save. Confirm or cancel the dialog to load it.");
}

void HandleSaveGameAction(std::string const &id, std::string const &data) {
	if (!ProbeScenarioIs(kSaveScenario, "save_game screen")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the save screen.");
		return;
	}

	int position = -1;
	if (!ParseSlot(id, data, "save_game", position)) {
		return;
	}

	int index = -1;
	if (!SlotIndex(id, position, index)) {
		return;
	}
	// Empty slots only. An occupied slot raises an overwrite confirmation and
	// destroys a save someone deliberately made; that is not something to hand
	// out until there is a decision about whether to allow it at all.
	if (ProbeBookmarkExists(index, "save_game occupied")) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "That slot already holds a save, and overwriting is not allowed. "
		    "Pick an empty slot.");
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
	if (!ProbeBookmarkExists(index, "save_game written")) {
		NeuroSendActionResult(id.c_str(), false, "The save did not go through.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Saved the game to that slot.");
}

// The collection screen opened to keep a line: the same grid as browsing, but a
// slot stores instead of playing.
void HandleSaveVoiceToSlotAction(std::string const &id,
                                 std::string const &data) {
	if (!ProbeScenarioIs(kVoiceScenario, "save_voice_to_slot screen")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the voice collection screen.");
		return;
	}

	int position = -1;
	if (!ParseSlot(id, data, "save_voice_to_slot", position)) {
		return;
	}

	if (!PressSlot(position, "save_voice_to_slot slot")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That slot is not selectable right now.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Kept the line in that slot.");
}

void HandlePlayVoiceAction(std::string const &id, std::string const &data) {
	if (!ProbeScenarioIs(kVoiceScenario, "play_voice screen")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the voice collection screen.");
		return;
	}

	int position = -1;
	if (!ParseSlot(id, data, "play_voice", position)) {
		return;
	}

	int index = -1;
	if (!SlotIndex(id, position, index)) {
		return;
	}
	if (!ProbeVoiceExists(index, "play_voice exists")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "There is no recorded line in that slot.");
		return;
	}
	if (!PressSlot(position, "play_voice slot")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That line is not selectable right now.");
		return;
	}
	// Replaying only plays audio: no dialog, no screen change, nothing to wait
	// for and nothing to undo.
	//
	// The line itself goes in the answer, because the sound does not reach her:
	// the API's voice channel is for games with voice chat and forbids sending
	// game audio at all. Without the text this action produces nothing she can
	// use.
	std::string record;
	std::string message = "Replaying that line.";
	if (ProbeVoiceLine(index, record) && !record.empty()) {
		std::string fields[4];
		size_t start = 0;
		for (int field = 0; field < 4; ++field) {
			size_t const tab = record.find('\t', start);
			fields[field] = record.substr(
			    start, tab == std::string::npos ? std::string::npos : tab - start);
			if (tab == std::string::npos) {
				break;
			}
			start = tab + 1;
		}
		if (!fields[2].empty()) {
			message += HasVisibleCharacters(fields[0])
			               ? " " + fields[0] + ": " + fields[2]
			               : " " + fields[2];
		}
		if (fields[3] == "1") {
			message += " Marked New: the most recently kept line.";
		}
	}
	NeuroSendActionResult(id.c_str(), true, message.c_str());
}

// The tab name, as its index into kSaveTabNames, or -1. The value is quoted in
// the payload, so matching the quoted form cannot collide with the key.
void HandleChooseDestinationAction(std::string const &id,
                                   std::string const &data) {
	int position = 0;
	if (!ParseSlot(id, data, "choose_destination", position)) {
		return;
	}
	if (!PressSlot(position, "choose_destination")) {
		// The source slot itself is the one the game refuses: a save cannot be
		// copied onto where it already is.
		NeuroSendActionResult(
		    id.c_str(), false,
		    "That slot cannot be the destination. The save being copied is "
		    "already in one of the slots, and that one is refused.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      "Chose that slot. The game is asking to confirm it.");
}

void HandleCancelCopyAction(std::string const &id) {
	// The Back button, which in this mode cancels and stays on the screen
	// rather than leaving it -- the same press the player uses, and the same
	// one Escape and a right click are wired to.
	if (!ProbeScreenButtonReady("back", "cancel_copy button") ||
	    !DispatchScreenButton("back", "cancel_copy dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "Could not cancel it.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      "Cancelled it. The save stays where it was.");
}

void HandleOpenEditMenuAction(std::string const &id, std::string const &data) {
	int position = 0;
	if (!ParseSlot(id, data, "open_edit_menu", position)) {
		return;
	}
	if (!ProbeEditButtonReady(position, "open_edit_menu button")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That slot has no edit menu: it is empty, or the "
		                      "menu cannot be opened for it.");
		return;
	}
	if (!DispatchEditButton(position, "open_edit_menu dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "The edit menu did not open.");
		return;
	}
	// The menu is raised by a scenario jump, so it is not up the instant the
	// press returns; where it ended up is read back rather than assumed.
	Sleep(kPageChangeSettleMs);
	int shown = 0;
	int items = 0;
	if (!ProbeEditMenuItems(shown, items, "open_edit_menu items")) {
		NeuroSendActionResult(id.c_str(), false, "The edit menu did not open.");
		return;
	}
	// Named by the plate, not by the cell. The cell number is what the press
	// needed, and printing it answered "slot 19" with "slot 9" -- the ninth cell
	// of the second page, which is a different thing entirely.
	int base = -1;
	int page = -1;
	std::string const label =
	    ProbeSaveWindow(base, page) ? PlateLabel(base + position) : "that slot";
	NeuroSendActionResult(id.c_str(), true,
	                      ("Opened the edit menu for " + label + ".").c_str());
}

constexpr int kEditCommentLimit = 120;

void HandleEditCommentAction(std::string const &id, std::string const &data) {
	std::string text;
	if (!ParseJsonString(data, "\"text\"", text)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "edit_comment requires a \"text\" string.");
		return;
	}
	if (text.size() > kEditCommentLimit) {
		// Refused rather than cut: a comment that comes back shorter than it was
		// sent is a quiet lie about what the save now says.
		NeuroSendActionResult(
		    id.c_str(), false,
		    ("That comment is longer than the " +
		     std::to_string(kEditCommentLimit) + " characters a slot holds.")
		        .c_str());
		return;
	}

	int shown = 0;
	int items = 0;
	if (!ProbeEditMenuItems(shown, items, "edit_comment items")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The edit menu is no longer open.");
		return;
	}
	if ((items & 1) == 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That entry is greyed out for this slot: comment.");
		return;
	}
	int position = 0;
	if (!ProbeEditMenuSlot(position, "edit_comment slot")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Cannot tell which save the menu belongs to.");
		return;
	}

	// The entry lights up all the same. It is not pressed -- pressing it opens
	// the window we cannot use -- but it is what was chosen, and every other
	// choice on every screen shows the cursor arriving before anything happens.
	// Left lit on purpose: the panel is about to close and take the entry with
	// it, which is what a real cursor resting on Comment would look like. Taking
	// the highlight off first only made it harder to see that anything was
	// chosen at all.
	if (DispatchEditMenuItemHover("comment", true, "edit_comment hover")) {
		Sleep(kHoverPreviewMs);
	}

	// The menu is dismissed the way cancelling dismisses it, because the entry
	// itself is not pressed.
	if (!DispatchCloseEditMenu("edit_comment close")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The edit menu is no longer open.");
		return;
	}
	Sleep(kPageChangeSettleMs);

	if (ProbeCommentWindowOpen("edit_comment window")) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "The game's own comment window is open; it has to be closed first.");
		return;
	}
	if (!DispatchSetComment(position, text, "edit_comment set")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The comment could not be written.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      text.empty() ? "Cleared the comment on that save."
	                                   : "Wrote that comment on the save.");
}

void HandleEditMenuItemAction(std::string const &id, int item) {
	int shown = 0;
	int items = 0;
	if (!ProbeEditMenuItems(shown, items, "edit menu items")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The edit menu is no longer open.");
		return;
	}
	if ((items & (1 << item)) == 0) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string((shown & (1 << item)) != 0
		                     ? "That entry is greyed out for this slot: "
		                     : "The edit menu has no such entry for this slot: ") +
		     kEditItemLabels[item] + ".")
		        .c_str());
		return;
	}
	if (!DispatchEditMenuItem(kEditItemControls[item], "edit menu dispatch")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That entry of the edit menu did not respond.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Chose it from the edit menu.");
}

void HandleCloseEditMenuAction(std::string const &id) {
	if (!DispatchCloseEditMenu("close_edit_menu dispatch")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The edit menu is no longer open.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Closed the edit menu.");
}

int ParseSaveTab(std::string const &data) {
	for (int index = 0; index < 4; ++index) {
		if (data.find(std::string("\"") + kSaveTabNames[index] + "\"") !=
		    std::string::npos) {
			return index;
		}
	}
	return -1;
}

void HandleSwitchPageAction(std::string const &id, std::string const &data) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"page\"", number) || number < 1) {
		NeuroSendActionResult(id.c_str(), false,
		                      "switch_page requires a \"page\" of 1 or more.");
		return;
	}
	char button[16];
	sprintf_s(button, "page%d", number);
	if (!ProbeScreenButtonReady(button, "switch_page button") ||
	    !DispatchScreenButton(button, "switch_page dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "This list has no such page.");
		return;
	}
	// The list redraws well after the press, and a press does not always land,
	// so where the window ended up is read back rather than assumed.
	Sleep(kPageChangeSettleMs);
	int base = -1;
	int page = -1;
	if (!ProbeSaveWindow(base, page) || page != number) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The list did not move to that page.");
		return;
	}
	std::string const message =
	    "Now on page " + std::to_string(number) + " of the list.";
	NeuroSendActionResult(id.c_str(), true, message.c_str());
}

void HandleSwitchTabAction(std::string const &id, std::string const &data) {
	int const index = ParseSaveTab(data);
	if (index < 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "switch_tab needs one of: load, save, quick, voice.");
		return;
	}
	char const *const name = kSaveTabNames[index];
	int visible = 0;
	int enabled = 0;
	if (!ProbeSaveTabs(visible, enabled)) {
		NeuroSendActionResult(id.c_str(), false, "The tab bar is not on screen.");
		return;
	}
	if ((visible & (1 << index)) == 0) {
		std::string const message =
		    std::string("The ") + name + " tab is the one already open.";
		NeuroSendActionResult(id.c_str(), false, message.c_str());
		return;
	}
	if ((enabled & (1 << index)) == 0) {
		std::string message = std::string("The ") + name + " tab is greyed out";
		message += (1 << index) == kSaveTabSave
		               ? ": there is no game in progress to save."
		               : ".";
		NeuroSendActionResult(id.c_str(), false, message.c_str());
		return;
	}
	std::string const button = std::string("to_") + name;
	bool const ok = ProbeScreenButtonReady(button.c_str(), "switch_tab button") &&
	                DispatchScreenButton(button.c_str(), "switch_tab dispatch");
	std::string const message =
	    ok ? std::string("Switched to the ") + name + " tab."
	       : std::string("Switching to the ") + name + " tab failed.";
	NeuroSendActionResult(id.c_str(), ok, message.c_str());
}

// Lists the saved games as Neuro will have to refer to them. Sent on entering
// the load screen rather than on demand: she has no way to look at the slots,
// and a number without a scene name behind it is not a choice.
// Which slots save_game will take. Occupied ones are refused, so listing the
// saves themselves here would only invite refusals.
// What is in the voice list. Each row carries the speaker and the scene it is
// from, which is what makes a slot number mean anything.
// What the ten cells on screen hold, in the numbering the actions use. The list
// behind them can run to a hundred entries; only the window is described,
// because only the window is what the slot numbers mean.
void SendWindowContext(std::string const &opening,
                       bool voice,
                       char const *closing) {
	int base = 0;
	int page = 0;
	std::string window;
	if (!ProbeSlotWindow(voice, base, page, window)) {
		return;
	}

	std::string filled;
	std::string empty;
	size_t start = 0;
	while (start < window.size()) {
		size_t const lineEnd = window.find('\n', start);
		std::string const line =
		    window.substr(start, lineEnd == std::string::npos ? std::string::npos
		                                                      : lineEnd - start);
		start = lineEnd == std::string::npos ? window.size() : lineEnd + 1;
		size_t const firstTab = line.find('\t');
		size_t const secondTab = firstTab == std::string::npos
		                             ? std::string::npos
		                             : line.find('\t', firstTab + 1);
		if (secondTab == std::string::npos) {
			continue;
		}
		// The probe numbers its rows 1..10 down the window; the screen numbers
		// them by the plates. Everything Neuro is told and everything she can
		// ask for uses the plates, so the row number is turned into one here.
		int row = 0;
		if (!ParseIntToken(line.substr(0, firstTab), row)) {
			continue;
		}
		std::string const slot = std::to_string(ScreenNumber(base + row - 1));
		std::string const what =
		    line.substr(firstTab + 1, secondTab - firstTab - 1);
		std::string const when = line.substr(secondTab + 1);
		if (what.empty() && when.empty()) {
			empty += empty.empty() ? slot : ", " + slot;
			continue;
		}
		filled += " ";
		filled += slot;
		filled += ") ";
		filled += what;
		filled += when.empty() ? ";" : ", " + when + ";";
	}

	std::string message = opening;
	if (page > 0) {
		message += " Page " + std::to_string(page) + ".";
	}
	if (filled.empty()) {
		message += voice ? " No lines are kept on this page."
		                 : " Every slot on this page is empty.";
	} else {
		message += voice ? " Lines on this page:" : " Saves on this page:";
		message += filled;
		if (!empty.empty()) {
			message += " Empty: " + empty + ".";
		}
	}
	if (closing != nullptr) {
		message += closing;
	}
	// The same list twice in a row says nothing the second time, and it happens:
	// a change arriving just after one announcement went out queues another, and
	// by the time it is due the screen is back where it was. Cleared on leaving
	// the screen, so coming back to the same page still announces it.
	if (message == g_LastAnnouncement) {
		return;
	}
	g_LastAnnouncement = message;
	NeuroSendContext(message.c_str(), false);
}

// Read every poll while a save screen is up: switching tabs is what changes the
// bar, and it happens while the screen stays open.
int ReadPayloadValue(bool editMenuOpen) {
	// While the menu stands, the screen behind it is asleep and its own reading
	// says nothing about what can be pressed. The menu is the whole payload.
	if (editMenuOpen) {
		int shown = 0;
		int items = 0;
		if (!ProbeEditMenuItems(shown, items, "edit menu payload")) {
			return 0;
		}
		SlotPayload value;
		value.editMenuOpen = true;
		value.editItems = items;
		value.editItemsShown = shown;
		return PackSlotPayload(value);
	}

	int visible = 0;
	int enabled = 0;
	if (!ProbeSaveTabs(visible, enabled)) {
		return 0;
	}
	int copyMode = 0;
	if (!ProbeCopyMode(copyMode, "copy mode")) {
		return 0;
	}
	int base = 0;
	int page = 0;
	// Same treatment as the tab bar above: a window that cannot be read is not
	// a window on page zero, and must not be folded into the payload as one.
	if (!ProbeSaveWindow(base, page)) {
		return 0;
	}
	int const pages = ProbeSavePageCount();
	SlotPayload value;
	value.visibleTabs = visible;
	value.enabledTabs = enabled;
	value.pageCount = pages;
	value.page = page;
	value.copyMode = copyMode;
	return PackSlotPayload(value);
}

// The tab bar is the same on all three modes, so its action is built in one
// place.
void AppendSwitchTabAction(ActionSet &set, int payload) {
	int const visible = UnpackSlotPayload(payload).visibleTabs;
	if (visible == 0) {
		return;
	}
	std::vector<char const *> values;
	std::string text = "Switch this screen to another of its tabs:";
	for (int index = 0; index < 4; ++index) {
		if ((visible & (1 << index)) == 0) {
			continue;
		}
		if (!values.empty()) {
			text += ",";
		}
		values.push_back(kSaveTabNames[index]);
		text += std::string(" ") + kSaveTabNames[index] + " (" +
		        kSaveTabHolds[index] + ")";
	}
	text += ". The tab that is open is not on the bar.";
	set.add("switch_tab", set.keep(text),
	        set.keep(EnumStringSchema("tab", values)));
}

// The page buttons under the same list. The quick tab has none: its ten slots
// are the whole list.
void AppendSwitchPageAction(ActionSet &set, int payload) {
	int const pages = UnpackSlotPayload(payload).pageCount;
	if (pages < 2) {
		return;
	}
	int const page = UnpackSlotPayload(payload).page;
	set.add("switch_page",
	        set.keep("Show another page of this list, 1 to " +
	                 std::to_string(pages) + ". Page " + std::to_string(page) +
	                 " is the one open. Slots are numbered as the screen numbers "
	                 "them, so another page brings another ten numbers."),
	        set.keep(BoundedIntSchema("page", pages)));
}

}  // namespace

namespace ScreenSlots {

bool ModeForScenario(std::string const &storage, Mode &mode) {
	if (storage == kLoadScenario) {
		mode = Mode::Load;
	} else if (storage == kSaveScenario) {
		mode = Mode::Save;
	} else if (storage == kVoiceScenario) {
		mode = Mode::Voice;
	} else {
		return false;
	}
	return true;
}

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	if (name == "load_save") {
		HandleLoadSaveAction(id, data);
	} else if (name == "read_save_slot") {
		HandleReadSaveSlotAction(id, data);
	} else if (name == "save_game") {
		HandleSaveGameAction(id, data);
	} else if (name == "save_voice_to_slot") {
		HandleSaveVoiceToSlotAction(id, data);
	} else if (name == "play_voice") {
		HandlePlayVoiceAction(id, data);
	} else if (name == "switch_tab") {
		HandleSwitchTabAction(id, data);
	} else if (name == "switch_page") {
		HandleSwitchPageAction(id, data);
	} else if (name == "choose_destination") {
		HandleChooseDestinationAction(id, data);
	} else if (name == "cancel_copy") {
		HandleCancelCopyAction(id);
	} else if (name == "open_edit_menu") {
		HandleOpenEditMenuAction(id, data);
	} else if (name == "close_edit_menu") {
		HandleCloseEditMenuAction(id);
	} else if (name == "edit_comment") {
		HandleEditCommentAction(id, data);
	} else if (name == "edit_copy") {
		HandleEditMenuItemAction(id, 1);
	} else if (name == "edit_move") {
		HandleEditMenuItemAction(id, 2);
	} else if (name == "edit_erase") {
		HandleEditMenuItemAction(id, 3);
	} else {
		return false;
	}
	return true;
}

// The menu takes the screen the way a confirmation dialog does: while it
// stands, the conductor sleeps and the controls behind it stop answering even
// to the cursor. So it gets its own set, and the choice is forced rather than
// merely offered.
void AppendEditMenuActions(ActionSet &set, int payload) {
	int const items = UnpackSlotPayload(payload).editItems;
	if ((items & 1) != 0) {
		// The one entry that is not a press. The game's own comment window is a
		// native dialog: it cannot be typed into from here, and the game does
		// not hand out a handle to it, so the text comes with the action and is
		// written the same way the game writes it once its window closes.
		set.add("edit_comment",
		        "Set the comment kept with this save, up to 120 characters. It "
		        "replaces the whole comment rather than adding to it; what the "
		        "save says now was given with the menu.",
		        kEditCommentSchema);
	}
	if ((items & 2) != 0) {
		set.add("edit_copy", "Copy this save into another slot, chosen next.",
		        kEditMenuItemSchema);
	}
	if ((items & 4) != 0) {
		set.add("edit_move", "Move this save into another slot, chosen next.",
		        kEditMenuItemSchema);
	}
	if ((items & 8) != 0) {
		set.add("edit_erase", "Erase this save. Asks for confirmation first.",
		        kEditMenuItemSchema);
	}
	set.add("close_edit_menu", "Close the edit menu without choosing.",
	        kGoBackSchema);
}

void ForceEditMenuChoice(int payload) {
	int const items = UnpackSlotPayload(payload).editItems;
	char const *names[5] = {};
	int count = 0;
	for (int index = 0; index < 4; ++index) {
		if ((items & (1 << index)) != 0) {
			names[count++] = kEditItemActions[index];
		}
	}
	names[count++] = "close_edit_menu";
	// Ephemeral: the menu stands only until something is chosen, and closing it
	// is always one of the choices.
	NeuroForceAction(
	    "The edit menu for that save is open, and nothing else on "
	    "the screen answers while it stands.",
	    "Choose one of its entries, or close it.", names, count, true);
}

void SendEditMenuContext(int payload) {
	SlotPayload const state = UnpackSlotPayload(payload);
	int const items = state.editItems;
	int const shown = state.editItemsShown;
	std::string offered;
	std::string refused;
	for (int index = 0; index < 4; ++index) {
		int const bit = 1 << index;
		std::string &list = (items & bit) != 0 ? offered : refused;
		if ((items & bit) == 0 && (shown & bit) == 0) {
			// Not on the menu at all for this slot; nothing to say about it.
			continue;
		}
		if (!list.empty()) {
			list += ", ";
		}
		list += kEditItemLabels[index];
	}

	std::string message = "The edit menu for that save is open.";
	message += offered.empty() ? " None of its entries can be used for this slot."
	                           : " Available: " + offered + ".";
	// What the comment says now. The game's own window opens with this text
	// already in it, so a player edits; without it here, the only thing that can
	// be done from outside is replace, and replacing what you cannot see means
	// wiping it by accident.
	if ((items & 1) != 0) {
		int position = 0;
		std::string comment;
		if (ProbeEditMenuSlot(position, "edit menu comment slot") &&
		    ProbeSlotComment(position, comment, "edit menu comment")) {
			message += comment.empty() ? " It has no comment yet."
			                           : " Its comment now reads: " + comment;
			if (!comment.empty()) {
				message +=
				    ". edit_comment replaces it whole, so send it back with the "
				    "change in it to keep the rest.";
			}
		}
	}
	if (!refused.empty()) {
		// Said plainly, because the entries are on screen: without this, an
		// entry the game draws and refuses is indistinguishable from one that
		// was never offered.
		message +=
		    " Also on the menu but greyed out for this slot: " + refused + ".";
	}
	NeuroSendContext(message.c_str(), false);
}

void AppendActions(ActionSet &set, Mode mode, int payload) {
	int const copyMode = UnpackSlotPayload(payload).copyMode;
	// The ten cells on screen, numbered the way the plates under them are. The
	// window base follows the page, so both the schema's bounds and the wording
	// change with it -- which is right: "slot 22" means one save, while "slot 2"
	// means a different one on every page.
	int const base = UnpackSlotPayload(payload).page * kVisibleSlots;
	std::string const range =
	    std::to_string(ScreenNumber(base)) + " to " +
	    std::to_string(ScreenNumber(base + kVisibleSlots - 1));
	char const *const slotSchema = set.keep(RangeIntSchema(
	    "slot", ScreenNumber(base), ScreenNumber(base + kVisibleSlots - 1)));
	if (copyMode != 0) {
		// The screen keeps its grid but every press means something else: the
		// slot is the destination now, not a save to load or a place to write.
		// Registering the ordinary slot action here would name the wrong deed.
		std::string const verb(kCopyModeVerbs[copyMode]);
		// Built, not literal, so the set has to own them: the registration call
		// reads these pointers after this function has returned.
		set.add("choose_destination",
		        set.keep("Choose the slot the save is " + verb +
		                 " into, by its number on screen, " + range +
		                 ". The game asks to confirm, and asks again if the slot "
		                 "already holds a save."),
		        slotSchema);
		set.add("cancel_copy",
		        set.keep("Cancel it: the save is not " + verb +
		                 " anywhere, and the screen goes back to normal."),
		        kGoBackSchema);
		// The destination may be on any page of any tab, so both stay. Moving to
		// the Voice tab is the one that does not: the game cancels the operation
		// when it opens.
		AppendSwitchTabAction(set, payload);
		AppendSwitchPageAction(set, payload);
		return;
	}

	set.add("go_back",
	        "Leave the current screen and return to the one it was opened from.",
	        kGoBackSchema);
	if (mode != Mode::Voice) {
		set.add("return_to_title",
		        "Leave the game and go back to the title screen. Asks for "
		        "confirmation first.",
		        kReturnToTitleSchema);
	}
	// Reading a slot is not loading it, so it is offered on the save screen too:
	// the question "what is already in slot 4" comes up most when about to write
	// over it.
	if (mode != Mode::Voice) {
		set.add("read_save_slot",
		        set.keep("Read everything one slot holds -- its scene, when it "
		                 "was saved, the comment written on it and the line it "
		                 "was saved at -- by its number on screen, " +
		                 range + "."),
		        slotSchema);
	}
	if (mode == Mode::Load) {
		set.add("load_save",
		        set.keep("Load a saved game by its number on screen, " + range +
		                 ". Asks for confirmation first."),
		        slotSchema);
	} else if (mode == Mode::Save) {
		set.add("save_game",
		        set.keep("Save the current progress into an empty slot, by its "
		                 "number on screen, " +
		                 range + ". Occupied slots are refused."),
		        slotSchema);
	} else if (ProbeVoiceSaveMode("voice mode")) {
		// The same grid, opposite meanings: opened to keep the current line, a
		// slot stores it; opened from Extra, a slot plays what is already kept.
		set.add("save_voice_to_slot",
		        set.keep("Keep the line in one of the ten slots on screen, by its "
		                 "number, " +
		                 range + "."),
		        slotSchema);
	} else {
		set.add(
		    "play_voice",
		    set.keep("Replay a kept line by its number on screen, " + range + "."),
		    slotSchema);
	}
	AppendSwitchTabAction(set, payload);
	AppendSwitchPageAction(set, payload);
	// Offered on every tab: the menu belongs to the slot, not to what the
	// screen was opened for. Which of its entries apply is decided when it
	// opens, so nothing about the slot is checked here.
	set.add("open_edit_menu",
	        set.keep("Open the edit menu of a saved slot, by its number on "
	                 "screen, " +
	                 range + ": comment, copy, move or erase."),
	        slotSchema);
}

// The announcement is held back until the list stops moving.
//
// A page change is one message: the list, the slots on it and what they hold.
// Turning pages with the mouse wheel walks through them one by one, and
// seventeen seconds of it produced twenty-seven of these -- each one prompting
// Neuro, and twenty-six of them describing a page she was already off. Waiting
// for the window to settle turns a spin of the wheel into the one announcement
// that is still true when it arrives.
//
// Anything a call of hers does is answered at once as always: this delays what
// the screen says about itself, not what her actions return.
namespace {
constexpr ULONGLONG kSettleMs = 500;

bool g_AnnouncementDue = false;
Mode g_PendingMode = Mode::Load;
int g_PendingPayload = 0;
ULONGLONG g_PendingSince = 0;
}  // namespace

void QueueEnteringContext(Mode mode, int payload) {
	g_AnnouncementDue = true;
	g_PendingMode = mode;
	g_PendingPayload = payload;
	// Restamped on every change, so a burst collapses into the last one.
	g_PendingSince = GetTickCount64();
}

void ForgetPendingContext() {
	g_AnnouncementDue = false;
	g_LastAnnouncement.clear();
}

void SendPendingContext() {
	if (!g_AnnouncementDue || GetTickCount64() - g_PendingSince < kSettleMs) {
		return;
	}
	g_AnnouncementDue = false;
	SendEnteringContext(g_PendingMode, g_PendingPayload);
}

void SendEnteringContext(Mode mode, int payload) {
	int const copyMode = UnpackSlotPayload(payload).copyMode;
	if (copyMode != 0) {
		std::string const verb(kCopyModeVerbs[copyMode]);
		// What the game itself puts in its help window at this moment is one
		// line: "Please choose a location to copy to." The rest of this is what
		// the screen shows but does not say -- the Edit buttons are gone, the
		// source slot refuses to be the destination, and the Voice tab cancels.
		NeuroSendContext(
		    ("A save is waiting to be " + verb +
		     ": choose the slot it goes into, on any page of any tab. The slot it "
		     "is already in cannot be chosen, and the edit menus are away until "
		     "this is settled. Opening the Voice tab cancels it.")
		        .c_str(),
		    false);
		return;
	}

	// Page 0 is the quick tab: the same screen holding the ten quick saves
	// instead of the numbered list.
	bool const quick = UnpackSlotPayload(payload).page == 0;
	if (mode == Mode::Load) {
		SendWindowContext(
		    quick ? "The quick saves are open." : "The load screen is open.", false,
		    nullptr);
	} else if (mode == Mode::Save) {
		SendWindowContext(
		    quick ? "The quick save slots are open." : "The save screen is open.",
		    false, " Occupied slots cannot be overwritten.");
	} else if (!ProbeVoiceSaveMode("voice mode context")) {
		// Nothing to list when the screen is open to keep a line: the slots are
		// where it would go, not what is already there.
		SendWindowContext("The voice collection is open.", true, nullptr);
	}
}

bool PayloadHasEditMenu(int payload) {
	return UnpackSlotPayload(payload).editMenuOpen;
}

bool ReadPayload(ScreenSnapshot const &screen, int &payload) {
	payload = ReadPayloadValue(screen.editMenuReady);
	return true;
}

}  // namespace ScreenSlots
