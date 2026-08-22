#include "ScreenGallery.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
#include "Logging.h"
#include "NeuroClient.h"

namespace {

// The grid is nine squares at most and four pages; the last page carries fewer
// squares, and the count for the page rides in the payload rather than being
// assumed.
constexpr int kSquares = 9;
constexpr int kPages = 4;
constexpr int kScrollMask = 0xf;
constexpr int kViewOpenFlag = 1 << 4;
// Set while the open picture still has further views to step through.
constexpr int kMoreViewsFlag = 1 << 5;
// How many cells this page uses, and the two counts the actions advertise as
// maximums. All of them ride in the payload so that a change re-registers.
constexpr int kSquareShift = 6;
constexpr int kSquareMask = 0xf;
constexpr int kPageShift = 11;
constexpr int kPageMask = 0x1f;
constexpr int kTrackShift = 16;
constexpr int kTrackMask = 0x3f;

char const *const kScrollNames[] = {"music_line_up", "music_line_down",
                                    "music_page_up", "music_page_down"};
char const *const kScrollText[] = {
    "Move one entry up the music list.", "Move one entry down the music list.",
    "Move one page up the music list.", "Move one page down the music list."};
char const *const kScrollButtons[] = {"up", "down", "pageup", "pagedown"};
char const *const kScrollDone[] = {
    "Moved up the music list.", "Moved down the music list.",
    "Moved a page up the music list.", "Moved a page down the music list."};
char const *const kScrollRefused[] = {
    "The music list is already at the top.",
    "The music list is already at the bottom.",
    "The music list is already at the top.",
    "The music list is already at the bottom."};

void HandleViewCgAction(std::string const &id, std::string const &data) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"square\"", number) || number < 1 ||
	    number > kSquares) {
		NeuroSendActionResult(id.c_str(), false,
		                      "view_cg requires a \"square\" between 1 and 9.");
		return;
	}
	int const slot = number - 1;
	if (!ProbeStateIs("cgmode", "view_cg state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the Extra screen.");
		return;
	}
	// The page decides how many squares there are, so the bound is read now
	// rather than taken from the schema.
	int page = 0, pages = 0, squares = 0, tracks = 0, scroll = 0, variants = 0;
	bool open = false;
	if (!ProbeGallery(page, pages, squares, tracks, scroll, open, variants)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Cannot tell what the gallery is showing.");
		return;
	}
	if (number > squares) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    ("This page has only " + std::to_string(squares) + " squares.")
		        .c_str());
		return;
	}

	char button[16];
	sprintf_s(button, "item%d", slot);
	HoverGalleryItem("item", slot, "view_cg hover");
	Sleep(kHoverPreviewMs);
	if (!ProbeScreenButtonReady(button, "view_cg button") ||
	    !DispatchScreenButton(button, "view_cg dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "That square did not respond.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened that picture.");
}

void HandleNextCgViewAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "next_cg_view does not accept arguments.");
		return;
	}
	if (!ProbeStateIs("cgmode", "next_cg_view state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the Extra screen.");
		return;
	}
	if (!DispatchGalleryNextView("next_cg_view dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "There is no picture open.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      "Moved to the next view of that picture.");
}

void HandleCloseCgAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "close_cg does not accept arguments.");
		return;
	}
	if (!ProbeStateIs("cgmode", "close_cg state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the Extra screen.");
		return;
	}
	if (!DispatchGalleryExit("close_cg dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "There is no picture open.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Closed the picture.");
}

void HandleGalleryPageAction(std::string const &id, std::string const &data) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"page\"", number) || number < 1 ||
	    number > kPages) {
		NeuroSendActionResult(id.c_str(), false,
		                      "gallery_page requires a \"page\" between 1 and 4.");
		return;
	}
	if (!ProbeStateIs("cgmode", "gallery_page state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the Extra screen.");
		return;
	}
	char button[16];
	sprintf_s(button, "page%d", number - 1);
	if (!ProbeScreenButtonReady(button, "gallery_page button") ||
	    !DispatchScreenButton(button, "gallery_page dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "Could not reach that page.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Switched the gallery page.");
}

void HandlePlayMusicAction(std::string const &id, std::string const &data) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"track\"", number) || number < 1) {
		NeuroSendActionResult(id.c_str(), false,
		                      "play_music requires a \"track\" of 1 or more.");
		return;
	}
	if (!ProbeStateIs("cgmode", "play_music state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the Extra screen.");
		return;
	}
	int page = 0, pages = 0, squares = 0, tracks = 0, scroll = 0, variants = 0;
	bool open = false;
	if (!ProbeGallery(page, pages, squares, tracks, scroll, open, variants) ||
	    number > tracks) {
		NeuroSendActionResult(id.c_str(), false, "There is no such track.");
		return;
	}
	HoverGalleryItem("sitem", number - 1, "play_music hover");
	if (!DispatchGalleryItem("mselect", number - 1, "play_music dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "That track did not respond.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Playing that track.");
}

}  // namespace

namespace ScreenGallery {

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	if (name == "view_cg") {
		HandleViewCgAction(id, data);
	} else if (name == "next_cg_view") {
		HandleNextCgViewAction(id, data);
	} else if (name == "close_cg") {
		HandleCloseCgAction(id, data);
	} else if (name == "gallery_page") {
		HandleGalleryPageAction(id, data);
	} else if (name == "play_music") {
		HandlePlayMusicAction(id, data);
	} else {
		for (int index = 0; index < 4; ++index) {
			if (name != kScrollNames[index]) {
				continue;
			}
			HandleScreenButtonAction(id, data, kScrollNames[index], "cgmode",
			                         kScrollButtons[index], kScrollDone[index],
			                         kScrollRefused[index]);
			return true;
		}
		return false;
	}
	return true;
}

void AppendActions(ActionSet &set, int payload) {
	// While a picture is up it covers the grid, so nothing behind it applies.
	if ((payload & kViewOpenFlag) != 0) {
		set.add("close_cg", "Close the picture and go back to the grid.",
		        kGalleryViewSchema);
		// Offered only while that picture still has further views; on the last
		// one the game itself returns to the grid.
		if ((payload & kMoreViewsFlag) != 0) {
			set.add("next_cg_view", "Show the next view of the open picture.",
			        kGalleryViewSchema);
		}
		return;
	}

	set.add("go_back", "Leave the Extra screen and return to the title.",
	        kGoBackSchema);
	// The grid's last page carries fewer squares, so both the wording and the
	// schema's maximum follow the page rather than assuming nine everywhere.
	int const squares = (payload >> kSquareShift) & kSquareMask;
	if (squares > 0) {
		set.add("view_cg",
		        set.keep("Open one of the " + std::to_string(squares) +
		                 " squares of the picture grid on this page, numbered "
		                 "from the top left."),
		        set.keep(BoundedIntSchema("square", squares)));
	}
	int const pages = (payload >> kPageShift) & kPageMask;
	if (pages > 0) {
		set.add("gallery_page",
		        set.keep("Switch the picture grid to one of its " +
		                 std::to_string(pages) + " pages."),
		        set.keep(BoundedIntSchema("page", pages)));
	}
	int const tracks = (payload >> kTrackShift) & kTrackMask;
	if (tracks > 0) {
		set.add("play_music",
		        set.keep("Play one of the " + std::to_string(tracks) +
		                 " tracks in the music list beside the grid."),
		        set.keep(BoundedIntSchema("track", tracks)));
	}
	int const scrollMask = payload & kScrollMask;
	for (int index = 0; index < 4; ++index) {
		if ((scrollMask & (1 << index)) != 0) {
			set.add(kScrollNames[index], kScrollText[index], kMusicScrollSchema);
		}
	}
}

bool ReadPayload(int &payload) {
	int page = 0, pages = 0, squares = 0, tracks = 0, scroll = 0, variants = 0;
	bool open = false;
	if (!ProbeGallery(page, pages, squares, tracks, scroll, open, variants)) {
		return false;
	}
	payload = scroll | (open ? kViewOpenFlag : 0) |
	          (variants > 0 ? kMoreViewsFlag : 0) | (squares << kSquareShift) |
	          (pages << kPageShift) | (tracks << kTrackShift);
	return true;
}

}  // namespace ScreenGallery
