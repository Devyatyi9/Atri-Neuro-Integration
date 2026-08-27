#include "ScreenSlots.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "Logging.h"
#include "NeuroClient.h"

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
constexpr int kSaveTabSave = 1 << 0;
constexpr int kSaveTabLoad = 1 << 1;
constexpr int kSaveTabQuick = 1 << 2;
constexpr int kSaveTabVoice = 1 << 3;
constexpr int kSaveTabMask = 0xf;
constexpr int kSaveTabEnabledShift = 4;
// How many pages the list has and which one is open, in the same value: the
// page decides which ten slots the numbers refer to, so moving it has to
// re-register and re-describe them.
constexpr int kSavePageCountShift = 8;
constexpr int kSavePageShift = 14;
constexpr int kSavePageMask = 0x3f;
// The per-slot edit menu: whether it is up, and which of its four entries can
// be pressed. Both belong in the payload because both change what the screen
// offers -- while the menu stands, nothing else on the screen can be used.
constexpr int kEditMenuOpen = 1 << 20;
constexpr int kEditItemsShift = 21;
constexpr int kEditItemsMask = 0xf;
// Drawn but refused is its own state here: the game greys an entry out for the
// slot rather than leaving it off the menu, and Neuro is told which is which.
constexpr int kEditItemsShownShift = 25;

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
	if (!ParseJsonInteger(data, "\"slot\"", number) || number < 1 ||
	    number > kVisibleSlots) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " requires a \"slot\" from 1 to " +
		     std::to_string(kVisibleSlots) + ", counting the slots on screen.")
		        .c_str());
		return false;
	}
	position = number - 1;
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

void HandleLoadSaveAction(std::string const &id, std::string const &data) {
	int position = -1;
	if (!ParseSlot(id, data, "load_save", position)) {
		return;
	}

	if (!ProbeScenarioIs(kLoadScenario, "load_save screen")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Saves can only be loaded from the load screen.");
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
	    "Selected the save; confirm or cancel the dialog to load it.");
}

void HandleSaveGameAction(std::string const &id, std::string const &data) {
	int position = -1;
	if (!ParseSlot(id, data, "save_game", position)) {
		return;
	}

	if (!ProbeScenarioIs(kSaveScenario, "save_game screen")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Saving is only possible from the save screen.");
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
	int position = -1;
	if (!ParseSlot(id, data, "save_voice_to_slot", position)) {
		return;
	}
	if (!ProbeScenarioIs(kVoiceScenario, "save_voice_to_slot screen")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the voice collection screen.");
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
	int position = -1;
	if (!ParseSlot(id, data, "play_voice", position)) {
		return;
	}

	if (!ProbeScenarioIs(kVoiceScenario, "play_voice screen")) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "Voice lines can only be replayed from the voice screen.");
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
	NeuroSendActionResult(id.c_str(), true, "Replaying that line.");
}

// The tab name, as its index into kSaveTabNames, or -1. The value is quoted in
// the payload, so matching the quoted form cannot collide with the key.
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
	NeuroSendActionResult(
	    id.c_str(), true,
	    ("Opened the edit menu for slot " + std::to_string(position + 1) + ".")
	        .c_str());
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
		std::string const slot = line.substr(0, firstTab);
		std::string const what =
		    line.substr(firstTab + 1, secondTab - firstTab - 1);
		std::string const when = line.substr(secondTab + 1);
		if (what.empty() && when.empty()) {
			empty += empty.empty() ? slot : ", " + slot;
			continue;
		}
		filled += " " + slot + ") " + what;
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
		return kEditMenuOpen | ((items & kEditItemsMask) << kEditItemsShift) |
		       ((shown & kEditItemsMask) << kEditItemsShownShift);
	}

	int visible = 0;
	int enabled = 0;
	if (!ProbeSaveTabs(visible, enabled)) {
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
	return (visible & kSaveTabMask) |
	       ((enabled & kSaveTabMask) << kSaveTabEnabledShift) |
	       ((pages & kSavePageMask) << kSavePageCountShift) |
	       ((page & kSavePageMask) << kSavePageShift);
}

// The tab bar is the same on all three modes, so its action is built in one
// place.
void AppendSwitchTabAction(ActionSet &set, int payload) {
	int const visible = payload & kSaveTabMask;
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
	int const pages = (payload >> kSavePageCountShift) & kSavePageMask;
	if (pages < 2) {
		return;
	}
	int const page = (payload >> kSavePageShift) & kSavePageMask;
	set.add("switch_page",
	        set.keep("Show another page of this list, 1 to " +
	                 std::to_string(pages) + ". Page " + std::to_string(page) +
	                 " is the one open, and the ten slot numbers always mean the "
	                 "ten slots of the page on screen."),
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
	} else if (name == "open_edit_menu") {
		HandleOpenEditMenuAction(id, data);
	} else if (name == "close_edit_menu") {
		HandleCloseEditMenuAction(id);
	} else if (name == "edit_comment") {
		HandleEditMenuItemAction(id, 0);
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
	int const items = (payload >> kEditItemsShift) & kEditItemsMask;
	if ((items & 1) != 0) {
		set.add("edit_comment",
		        "Edit the comment kept with this save. Opens a text entry.",
		        kEditMenuItemSchema);
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
	int const items = (payload >> kEditItemsShift) & kEditItemsMask;
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
	NeuroForceAction("The edit menu for that save is open, and nothing else on "
	                 "the screen answers while it stands.",
	                 "Choose one of its entries, or close it.", names, count,
	                 true);
}

void SendEditMenuContext(int payload) {
	int const items = (payload >> kEditItemsShift) & kEditItemsMask;
	int const shown = (payload >> kEditItemsShownShift) & kEditItemsMask;
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
	if (!refused.empty()) {
		// Said plainly, because the entries are on screen: without this, an
		// entry the game draws and refuses is indistinguishable from one that
		// was never offered.
		message += " Also on the menu but greyed out for this slot: " + refused +
		           ".";
	}
	NeuroSendContext(message.c_str(), false);
}

void AppendActions(ActionSet &set, Mode mode, int payload) {
	set.add("go_back",
	        "Leave the current screen and return to the one it was opened from.",
	        kGoBackSchema);
	if (mode != Mode::Voice) {
		set.add("return_to_title",
		        "Leave the game and go back to the title screen. Asks for "
		        "confirmation first.",
		        kReturnToTitleSchema);
	}
	if (mode == Mode::Load) {
		set.add("load_save",
		        "Load a saved game by its slot number on screen, 1 to 10. Asks "
		        "for confirmation first.",
		        kSlotSchema);
	} else if (mode == Mode::Save) {
		set.add("save_game",
		        "Save the current progress into an empty slot, by its slot number "
		        "on screen, 1 to 10. Occupied slots are refused.",
		        kSlotSchema);
	} else if (ProbeVoiceSaveMode("voice mode")) {
		// The same grid, opposite meanings: opened to keep the current line, a
		// slot stores it; opened from Extra, a slot plays what is already kept.
		set.add("save_voice_to_slot",
		        "Keep the line in one of the ten slots on screen, by its number.",
		        kSlotSchema);
	} else {
		set.add("play_voice",
		        "Replay a kept line by its slot number on screen, 1 to 10.",
		        kSlotSchema);
	}
	AppendSwitchTabAction(set, payload);
	AppendSwitchPageAction(set, payload);
	// Offered on every tab: the menu belongs to the slot, not to what the
	// screen was opened for. Which of its entries apply is decided when it
	// opens, so nothing about the slot is checked here.
	set.add("open_edit_menu",
	        "Open the edit menu of a saved slot, by its slot number on screen: "
	        "comment, copy, move or erase.",
	        kEditMenuSchema);
}

void SendEnteringContext(Mode mode, int payload) {

	// Page 0 is the quick tab: the same screen holding the ten quick saves
	// instead of the numbered list.
	bool const quick = ((payload >> kSavePageShift) & kSavePageMask) == 0;
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
	return (payload & kEditMenuOpen) != 0;
}

bool ReadPayload(ScreenSnapshot const &screen, int &payload) {
	payload = ReadPayloadValue(screen.editMenuReady);
	return true;
}

}  // namespace ScreenSlots
