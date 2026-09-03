#include "ScreenGallery.h"

#include <windows.h>

#include <cstdio>  // IWYU pragma: keep -- declares sprintf_s
#include <string>

#include "ActionCommon.h"
#include "ActionJson.h"
#include "ActionProbes.h"
#include "ActionSchemas.h"
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
// Which axes the open picture will move along -- bit 0 horizontal, bit 1
// vertical. Zero for every view that fits the frame, which is nearly all of
// them: only the first record's second view is larger than the window.
constexpr int kCanScrollShift = 22;
constexpr int kCanScrollMask = 0x3;
constexpr int kTrackShift = 16;
constexpr int kTrackMask = 0x3f;

// The list slides to its new place over about 200 ms; reading before it comes
// to rest gives back where it started.
constexpr int kMusicScrollSettleMs = 300;

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
	GalleryState gallery;
	if (!ProbeGallery(gallery)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Cannot tell what the gallery is showing.");
		return;
	}
	if (number > gallery.squareCount) {
		NeuroSendActionResult(id.c_str(), false,
		                      ("This page has only " +
		                       std::to_string(gallery.squareCount) + " squares.")
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

void HandleMusicVolumeAction(std::string const &id, std::string const &data) {
	int wanted = 0;
	if (!ParseJsonInteger(data, "\"volume\"", wanted) || wanted < 0 ||
	    wanted > 100) {
		NeuroSendActionResult(id.c_str(), false,
		                      "set_music_volume needs a \"volume\" from 0 to 100.");
		return;
	}

	int before = -1;
	ProbeMusicVolume(before, "music volume before");
	if (!DispatchMusicVolume(wanted, "music volume")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The volume slider did not answer.");
		return;
	}

	// Read back, because writing only the knob would move the picture of the
	// volume without moving the volume, and that is exactly the mistake this
	// action exists to avoid.
	int after = -1;
	if (!ProbeMusicVolume(after, "music volume after") || after != wanted) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The volume did not take that value.");
		return;
	}
	std::string message =
	    "Music volume is now " + std::to_string(after) + " of 100";
	message += before >= 0 && before != after
	               ? ", was " + std::to_string(before) + "."
	               : ".";
	NeuroSendActionResult(id.c_str(), true, message.c_str());
}

// What the buttons in the music list say. The game stores only the Japanese
// titles, and the English on screen is baked into the interface pictures, so
// the two are joined here by sound file name -- the one handle both sides
// share.
//
// Only the twenty numbered tracks are here. The four at the end of the list
// -- the opening and ending themes -- are drawn from the stored titles
// themselves, in Japanese, so those fall through to the fallback and match
// the screen that way.
//
// Where the two disagree the screen wins: BGM11 is Working on the album and
// in the game's own data, and Get Working! on the button.
struct TrackName {
	char const *storage;
	char const *english;
};
constexpr TrackName kTrackNames[] = {
    {"BGM01", "The Sunken City"},
    {"BGM02", "Hope's Light"},
    {"BGM03", "Tranquil Moments"},
    {"BGM04", "Vertigo"},
    {"BGM05", "The Face I See When I Close My Eyes"},
    {"BGM06", "The Happy Little Killer Robot"},
    {"BGM07", "Good Morning, Sunshine"},
    {"BGM08", "The Brink Of Death"},
    {"BGM09", "Walking In The Twilight"},
    {"BGM10", "A Soothing Breeze"},
    {"BGM11", "Get Working!"},
    {"BGM12", "Halation"},
    {"BGM13", "Dumb Heap Of Scrap!"},
    {"BGM14", "The Children Play"},
    {"BGM15", "A Lull In The Sea"},
    {"BGM16", "Weigh Anchor!"},
    {"BGM17", "Shadowy Footsteps"},
    {"BGM18", "Sunshine On Deck"},
    {"BGM19", "When I See The Sparkle Of Your Tears"},
    {"BGM20", "Days Of Love"},
};

// Falls back to whatever the game itself calls the track, so a save that
// unlocks something this table has never seen is still described -- in
// Japanese -- rather than left as a bare file name.
std::string TrackTitle(std::string const &storage,
                       std::string const &fallback) {
	for (TrackName const &name : kTrackNames) {
		if (storage == name.storage) {
			return name.english;
		}
	}
	return fallback;
}

// Turns the probe's rows into the sentence Neuro reads. The list is worth
// spelling out because only part of it is on screen at a time, and because the
// words on the buttons are pictures: nothing about them can be read back.
std::string DescribeMusicList() {
	std::string rows;
	if (!ProbeMusicList(rows, "music list") || rows.empty()) {
		return std::string();
	}
	std::string::size_type cursor = rows.find('\n');
	if (cursor == std::string::npos) {
		return std::string();
	}
	std::string const total = rows.substr(0, cursor);
	std::string text = "The music list holds " + total +
	                   " tracks and shows a few at a time. Showing now:";
	while (cursor != std::string::npos) {
		std::string::size_type const end = rows.find('\n', cursor + 1);
		std::string const line =
		    rows.substr(cursor + 1, end == std::string::npos ? std::string::npos
		                                                     : end - cursor - 1);
		cursor = end;
		// slot, file name, title, playing -- four fields, and a row missing any of
		// them is dropped rather than guessed at.
		std::string::size_type const first = line.find('\t');
		if (first == std::string::npos) {
			continue;
		}
		std::string::size_type const second = line.find('\t', first + 1);
		if (second == std::string::npos) {
			continue;
		}
		std::string::size_type const third = line.find('\t', second + 1);
		if (third == std::string::npos) {
			continue;
		}
		std::string const storage = line.substr(first + 1, second - first - 1);
		text += "\n" + line.substr(0, first) + ". " +
		        TrackTitle(storage, line.substr(second + 1, third - second - 1)) +
		        " (" + storage + ")";
		if (line.compare(third + 1, std::string::npos, "1") == 0) {
			text += " (playing)";
		}
	}
	text += "\nThose numbers are what switch_track takes.";
	return text;
}

void SendMusicListContext() {
	std::string const text = DescribeMusicList();
	if (!text.empty()) {
		NeuroSendContext(text.c_str(), false);
	}
}

void HandleMusicScrollBarAction(std::string const &id,
                                std::string const &data) {
	int wanted = 0;
	if (!ParseJsonInteger(data, "\"position\"", wanted) || wanted < 0 ||
	    wanted > 100) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "scroll_music_list needs a \"position\" from 0 to 100.");
		return;
	}

	int before = 0;
	int limit = 0;
	if (!ProbeMusicScroll(before, limit, "music scroll before") || limit <= 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The music list does not scroll here.");
		return;
	}
	// The bar counts in the game's own pixels, and how many there are depends on
	// how long the list is. Asking in hundredths keeps the action from having to
	// know that.
	int const target = (wanted * limit + 50) / 100;
	if (!DispatchMusicScroll(target, "music scroll")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The music list scroll bar did not answer.");
		return;
	}
	Sleep(kMusicScrollSettleMs);

	int after = 0;
	int stillLimit = 0;
	if (!ProbeMusicScroll(after, stillLimit, "music scroll after") ||
	    stillLimit <= 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The music list did not report where it stands.");
		return;
	}
	// Reported from where the bar actually came to rest rather than from what was
	// asked for: the game snaps to whole rows, so the two rarely match exactly.
	int const percent = (after * 100 + stillLimit / 2) / stillLimit;
	std::string message =
	    "The music list is at " + std::to_string(percent) + " of 100";
	message += after == before ? ", where it already was." : ".";
	NeuroSendActionResult(id.c_str(), true, message.c_str());
	if (after != before) {
		SendMusicListContext();
	}
}

void HandleScrollCgAction(std::string const &id, std::string const &data) {
	int horizontal = 0;
	int vertical = 0;
	bool const hasH = ParseJsonInteger(data, "\"horizontal\"", horizontal);
	bool const hasV = ParseJsonInteger(data, "\"vertical\"", vertical);
	if (!hasH && !hasV) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    "scroll_cg needs \"horizontal\" or \"vertical\", in steps.");
		return;
	}
	if (horizontal == 0 && vertical == 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That would not move the picture anywhere.");
		return;
	}

	CgScrollState before;
	if (!ProbeCgScroll(before, "scroll_cg before") || before.canScroll == 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "This picture fits the frame; there is nothing to "
		                      "move.");
		return;
	}
	if (horizontal != 0 && (before.canScroll & 1) == 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "This picture does not move sideways.");
		return;
	}
	if (vertical != 0 && (before.canScroll & 2) == 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "This picture does not move up or down.");
		return;
	}

	if (!DispatchCgScroll(horizontal, vertical, "scroll_cg")) {
		NeuroSendActionResult(id.c_str(), false, "The picture did not move.");
		return;
	}

	// Read back rather than assume: the game stops at the edges, so asking for
	// ten steps near the border moves fewer, and the answer should say where the
	// picture actually ended up.
	CgScrollState after;
	if (!ProbeCgScroll(after, "scroll_cg after")) {
		NeuroSendActionResult(id.c_str(), true, "Moved the picture.");
		return;
	}
	if (after.left == before.left && after.top == before.top) {
		NeuroSendActionResult(id.c_str(), false,
		                      "The picture is already at that edge.");
		return;
	}
	std::string message = "Moved the picture. It now sits " +
	                      std::to_string(after.left) + " of " +
	                      std::to_string(after.maxLeft) + " across and " +
	                      std::to_string(after.top) + " of " +
	                      std::to_string(after.maxTop) + " down.";
	NeuroSendActionResult(id.c_str(), true, message.c_str());
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

void HandleSwitchTrackAction(std::string const &id, std::string const &data) {
	int number = -1;
	if (!ParseJsonInteger(data, "\"track\"", number) || number < 1) {
		NeuroSendActionResult(id.c_str(), false,
		                      "switch_track requires a \"track\" of 1 or more.");
		return;
	}
	if (!ProbeStateIs("cgmode", "switch_track state")) {
		NeuroSendActionResult(id.c_str(), false,
		                      "That only works on the Extra screen.");
		return;
	}
	GalleryState gallery;
	if (!ProbeGallery(gallery) || number > gallery.trackCount) {
		NeuroSendActionResult(id.c_str(), false, "There is no such track.");
		return;
	}
	HoverGalleryItem("sitem", number - 1, "switch_track hover");
	if (!DispatchGalleryItem("mselect", number - 1, "switch_track dispatch")) {
		NeuroSendActionResult(id.c_str(), false, "That track did not respond.");
		return;
	}
	// Read back which track the player landed on rather than trusting the
	// press: the answer names it, and naming the wrong one would be worse
	// than naming none.
	std::string storage;
	std::string title;
	if (!ProbeMusicPlaying(storage, title, "switch_track playing")) {
		NeuroSendActionResult(id.c_str(), true, "Switched to that track.");
		return;
	}
	std::string const message =
	    "Now playing " + TrackTitle(storage, title) + " (" + storage + ").";
	NeuroSendActionResult(id.c_str(), true, message.c_str());
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
	} else if (name == "set_music_volume") {
		HandleMusicVolumeAction(id, data);
	} else if (name == "scroll_music_list") {
		HandleMusicScrollBarAction(id, data);
	} else if (name == "scroll_cg") {
		HandleScrollCgAction(id, data);
	} else if (name == "close_cg") {
		HandleCloseCgAction(id, data);
	} else if (name == "gallery_page") {
		HandleGalleryPageAction(id, data);
	} else if (name == "switch_track") {
		HandleSwitchTrackAction(id, data);
	} else {
		for (int index = 0; index < 4; ++index) {
			if (name != kScrollNames[index]) {
				continue;
			}
			HandleScreenButtonAction(id, data, kScrollNames[index], "cgmode",
			                         kScrollButtons[index], kScrollDone[index],
			                         kScrollRefused[index]);
			// The step buttons slide the list under the same buttons, so the rows
			// those numbers stand for are no longer the ones last announced.
			Sleep(kMusicScrollSettleMs);
			SendMusicListContext();
			return true;
		}
		return false;
	}
	return true;
}

void SendEnteringContext() {
	SendMusicListContext();
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
		int const axes = (payload >> kCanScrollShift) & kCanScrollMask;
		if (axes != 0) {
			// Only this view is bigger than the frame, so this is the only place
			// the picture can be moved at all.
			set.add("scroll_cg",
			        axes == 3 ? "Move the picture inside the frame: horizontal is "
			                    "positive to the right, vertical positive down, "
			                    "each in steps."
			        : axes == 1
			            ? "Move the picture left or right inside the frame, in "
			              "steps; positive is to the right."
			            : "Move the picture up or down inside the frame, in "
			              "steps; positive is down.",
			        kScrollCgSchema);
		}
		return;
	}

	set.add("go_back", "Leave the Extra screen and return to the title.",
	        kGoBackSchema);
	// The music side of this screen carries a volume slider, and it is there
	// whether or not anything is playing.
	set.add("set_music_volume",
	        "Set how loud the music player is, 0 to 100. It is the slider under "
	        "the track list.",
	        kMusicVolumeSchema);
	// The track list is longer than the frame it sits in, so the bar beside it is
	// the only way to reach the entries below the visible rows.
	set.add("scroll_music_list",
	        "Move the track list to a place along its scroll bar, 0 at the top "
	        "and 100 at the bottom. The list shows only part of itself at a "
	        "time.",
	        kMusicScrollBarSchema);
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
		// Not "play": the player is never silent, so the only thing a number
		// here can do is move it off whatever is already going.
		set.add("switch_track",
		        set.keep("Switch the music player to one of the " +
		                 std::to_string(tracks) +
		                 " tracks the list beside the grid is showing. "
		                 "Something is always playing; this changes which."),
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
	GalleryState gallery;
	if (!ProbeGallery(gallery)) {
		return false;
	}
	// Asked only while a picture is up: with the grid on screen there is no
	// layer to ask about, and this runs several times a second.
	int canScroll = 0;
	if (gallery.viewOpen) {
		CgScrollState scroll;
		if (ProbeCgScroll(scroll, "cg scroll")) {
			canScroll = scroll.canScroll;
		}
	}
	payload = gallery.scrollMask | (gallery.viewOpen ? kViewOpenFlag : 0) |
	          (gallery.variantsLeft > 0 ? kMoreViewsFlag : 0) |
	          (gallery.squareCount << kSquareShift) |
	          (gallery.pageCount << kPageShift) |
	          (gallery.trackCount << kTrackShift) |
	          ((canScroll & kCanScrollMask) << kCanScrollShift);
	return true;
}

}  // namespace ScreenGallery
