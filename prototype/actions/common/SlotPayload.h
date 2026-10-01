#pragma once

#include <string>

// The save, load and voice screens describe themselves to the action set with a
// single int, because the key that carries the state through is one number.
// This is what that number holds.
//
// Nothing outside this pair of files should shift or mask it by hand. Doing
// that by hand is what let fields land on each other, and the layout is dense
// enough that there is no room to be casual: the eight fields fill thirty-one
// bits with one to spare.
struct SlotPayload {
	// Which tabs the bar shows, and which of those can be pressed, as the
	// kSlotTab* bits below. The tab that is open has no button of its own.
	int visibleTabs = 0;
	int enabledTabs = 0;
	// How many pages the list has and which one is open. The page decides which
	// ten slots the action numbers refer to; a page of zero is the quick tab,
	// whose ten slots are the whole list.
	int pageCount = 0;
	int page = 0;
	// The per-slot edit menu. While it stands, nothing else on the screen can be
	// used, so these three are the whole payload and the rest reads as zero.
	bool editMenuOpen = false;
	// Which entries can be pressed, and which are drawn at all. Drawn but
	// refused is its own state: the game greys an entry out for the slot rather
	// than leaving it off the menu, and Neuro is told which is which.
	int editItems = 0;
	int editItemsShown = 0;
	// Copy, move and swap all put the screen into the same shape -- the next
	// slot pressed is the destination -- but the press means something different
	// in each, so which one it is travels too.
	int copyMode = 0;
};

// The bits of both tab fields.
constexpr int kSlotTabSave = 1 << 0;
constexpr int kSlotTabLoad = 1 << 1;
constexpr int kSlotTabQuick = 1 << 2;
constexpr int kSlotTabVoice = 1 << 3;

// A value too large for its field is cut to it rather than spilling into the
// next one, so a misread probe cannot silently rewrite an unrelated field.
int PackSlotPayload(SlotPayload const &value);
SlotPayload UnpackSlotPayload(int payload);

// The game writes a save's date as "2026/01/03 16:45". Everything else the
// plugin says about time reads "2026-01-03 16:45", so this one is brought into
// line; text that does not start with such a date is returned as it is.
std::string IsoDate(std::string const &text);
