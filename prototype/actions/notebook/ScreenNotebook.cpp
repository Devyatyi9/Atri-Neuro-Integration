#include "ScreenNotebook.h"

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>

#include "ActionJson.h"
#include "ActionSet.h"
#include "Logging.h"
#include "NeuroClient.h"
#include "NotebookGate.h"
#include "NotebookStore.h"
#include "PluginConfig.h"
#include "UtfConversion.h"

namespace {

// The notebook that is open, or was last: 1 or 2.
std::atomic<int> g_Book(1);

int Book() {
	return g_Book.load();
}

// What the story last said about the notebooks, packed so the main thread can
// leave it without a lock: bit 0 the first, bit 1 the second, then the reason.
constexpr int kAccessFirst = 1;
constexpr int kAccessSecond = 2;
constexpr int kAccessReasonShift = 2;
constexpr int kAccessUnknown = -1;
std::atomic<int> g_StoryAccess(kAccessUnknown);
// The access the last refresh acted on, to tell a change from a standing state.
std::atomic<int> g_FollowedAccess(kAccessUnknown);
// The scene the story stands in, and the one the last refresh saw. A notebook
// changes hands inside a scene; a change that comes with another scene is a
// save loaded or a jump, not something that happened.
std::atomic<int> g_StoryScene(-1);
std::atomic<int> g_FollowedScene(-1);
// Saves loaded and backlog jumps taken, and how many the last refresh had seen.
std::atomic<int> g_Jumps(0);
std::atomic<int> g_FollowedJumps(0);
// Until when a change in the notebooks is the jump landing, not news. The
// position can reach us a poll later than the jump itself.
std::atomic<ULONGLONG> g_QuietUntil(0);
constexpr ULONGLONG kJumpSettleMs = 2000;
// A dialog asking about a load or a jump is up; and, once it has closed, until
// when a move of the story's position means it was agreed to.
std::atomic<bool> g_JumpAsked(false);
std::atomic<ULONGLONG> g_JumpAnswerUntil(0);
// The game takes a while to land: the scene fades out and the next one loads.
constexpr ULONGLONG kJumpAnswerMs = 15000;
// Lines moved between two reports that reading does not cover.
constexpr int kJumpStride = 60;

int Pack(NotebookGate::Access const &access) {
	return (access.first ? kAccessFirst : 0) |
	       (access.second ? kAccessSecond : 0) |
	       (static_cast<int>(access.reason) << kAccessReasonShift);
}

NotebookGate::Access Unpack(int packed) {
	NotebookGate::Access access;
	if (packed == kAccessUnknown) {
		return access;
	}
	access.first = (packed & kAccessFirst) != 0;
	access.second = (packed & kAccessSecond) != 0;
	access.reason =
	    static_cast<NotebookGate::Reason>(packed >> kAccessReasonShift);
	return access;
}

// Notebooks kept from an earlier time through, as the access bits: the story
// no longer takes these away. Kept by the game with its system variables.
std::atomic<int> g_Unlocked(0);
// Bits an ending has earned that are not saved yet.
std::atomic<int> g_UnlockPending(0);

// What the story allows, plus what an earlier time through has kept.
int Effective(int packed) {
	if (packed == kAccessUnknown) {
		return packed;
	}
	NotebookGate::Access access = Unpack(packed);
	if (access.reason == NotebookGate::Reason::NotInStory) {
		return packed;
	}
	int const kept = g_Unlocked.load();
	access.first = access.first || (kept & kAccessFirst) != 0;
	access.second = access.second || (kept & kAccessSecond) != 0;
	if (access.Any()) {
		access.reason = NotebookGate::Reason::InHand;
	}
	return Pack(access);
}

// With the story gate off there is one notebook, always in hand.
NotebookGate::Access CurrentAccess() {
	if (!GetPluginConfig().NotebookStoryGate) {
		NotebookGate::Access access;
		access.first = true;
		access.reason = NotebookGate::Reason::InHand;
		return access;
	}
	return Unpack(Effective(g_StoryAccess.load()));
}

bool Has(NotebookGate::Access const &access, int book) {
	return book == 1 ? access.first : access.second;
}
// A page up to this size comes back in the result itself; a longer one goes
// to context, and the result only says which page is being read.
constexpr size_t kInlinePageBytes = 300;

char const *const kOpenDescription =
    "Open your notebook to read or write in it.";

std::mutex g_Mutex;
std::atomic<int> g_Open(0);
// Whether the notebook's rules have gone to context this run.
std::atomic<bool> g_RulesSaid(false);
// The page reading goes to when no page is named. Guarded by g_Mutex.
int g_CurrentPage = 1;

std::wstring NotebookRoot() {
	wchar_t path[MAX_PATH] = {};
	DWORD const length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring folder(path, length);
	size_t const slash = folder.find_last_of(L"\\/");
	folder = slash == std::wstring::npos ? L"." : folder.substr(0, slash);
	return folder + L"\\neuro-assets\\notebooks";
}

NotebookStore &Store() {
	static NotebookStore store(NotebookRoot());
	return store;
}

std::string PageRangeError() {
	return "\"page\" must be between 1 and " +
	       std::to_string(NotebookStore::kPageCount) + ".";
}

// An absent key is fine and leaves `page` alone; a present one must be valid.
bool ReadPageArgument(std::string const &data, int &page, bool &valid) {
	valid = true;
	if (data.find("\"page\"") == std::string::npos) {
		return false;
	}
	int value = 0;
	valid = ParseJsonInteger(data, "\"page\"", value) && value >= 1 &&
	        value <= NotebookStore::kPageCount;
	if (valid) {
		page = value;
	}
	return true;
}

// A present key must be a positive integer; an absent one leaves `value` alone.
bool ReadIntArgument(std::string const &data,
                     char const *key,
                     int &value,
                     bool &valid) {
	valid = true;
	if (data.find(key) == std::string::npos) {
		return false;
	}
	int parsed = 0;
	valid = ParseJsonInteger(data, key, parsed) && parsed >= 1;
	if (valid) {
		value = parsed;
	}
	return true;
}

// The page as Neuro reads it: when it was started and last written, then
// numbered lines with the margin notes lined up.
std::string FormatPage(int page, std::string const &utf8) {
	std::wstring wide;
	std::string lines = utf8;
	if (Utf8ToUtf16(utf8.data(), utf8.size(), wide)) {
		std::wstring const numbered = NotebookStore::NumberedLines(wide);
		Utf16ToUtf8(numbered.c_str(), numbered.size(), lines);
	}
	std::string header = "Page " + std::to_string(page);
	NotebookStore::PageTimes const times = Store().Times(Book(), page);
	if (times.written) {
		header += ", started " + times.created + ", last written " + times.modified;
	}
	return header + "\n" + lines;
}

std::string LineSpan(int first, int last) {
	return first == last
	           ? "line " + std::to_string(first)
	           : "lines " + std::to_string(first) + "-" + std::to_string(last);
}

// "1, 7-8, 99-100" from pages in order.
std::string PageRanges(std::vector<int> const &pages) {
	std::string out;
	for (size_t i = 0; i < pages.size(); ++i) {
		size_t end = i;
		while (end + 1 < pages.size() && pages[end + 1] == pages[end] + 1) {
			++end;
		}
		if (!out.empty()) {
			out += ", ";
		}
		out += std::to_string(pages[i]);
		if (end > i) {
			out += "-" + std::to_string(pages[end]);
		}
		i = end;
	}
	return out;
}

// 0 when every page has writing.
int FirstBlankPage(std::vector<int> const &written) {
	for (int page = 1; page <= NotebookStore::kPageCount; ++page) {
		if ((size_t)page > written.size() || written[page - 1] != page) {
			return page;
		}
	}
	return 0;
}

// The quoted text echoed back: whole when short, otherwise its start.
std::string Excerpt(std::string const &utf8) {
	constexpr int kShown = 40;
	std::wstring wide;
	if (!Utf8ToUtf16(utf8.data(), utf8.size(), wide) ||
	    NotebookStore::CountCharacters(wide) <= kShown) {
		return utf8;
	}
	size_t cut = 0;
	for (int taken = 0; taken < kShown && cut < wide.size(); ++taken) {
		cut += (wide[cut] >= 0xD800 && wide[cut] <= 0xDBFF) ? 2 : 1;
	}
	std::string start;
	Utf16ToUtf8(wide.c_str(), cut, start);
	return start + "...";
}

// Refusals shared by the actions that change a page.
bool AnswerRefusal(std::string const &id,
                   NotebookStore::WriteOutcome const &outcome,
                   int page,
                   int line) {
	std::string const where = "page " + std::to_string(page);
	std::string const which = "Line " + std::to_string(line);
	std::string message;
	switch (outcome.status) {
		case NotebookStore::Status::Ok:
			return false;
		case NotebookStore::Status::BadPage:
			message = PageRangeError();
			break;
		case NotebookStore::Status::LineTaken:
			message = which + " on " + where +
			          " already has writing. Only a blank line can be written on.";
			break;
		case NotebookStore::Status::NotEnoughLines:
			message = which + " on " + where + " has room for " +
			          std::to_string(outcome.blankLines) + " blank line" +
			          (outcome.blankLines == 1 ? "" : "s") +
			          " in a row; the text has " + std::to_string(outcome.textLines) +
			          ". Writing already there does not move.";
			break;
		case NotebookStore::Status::HasNote:
			message = which + " already has a note.";
			break;
		case NotebookStore::Status::TooLong:
			message = "A margin note holds up to " +
			          std::to_string(NotebookStore::kMarginNoteMax) + " characters.";
			break;
		case NotebookStore::Status::NotFound:
			message = "Could not find that text on " + where +
			          ". Give it exactly as written, capitals included.";
			break;
		case NotebookStore::Status::AlreadyStruck:
			message = "That text is already struck through.";
			break;
		case NotebookStore::Status::NoSuchOccurrence:
			message = "Found only " + std::to_string(outcome.matches) +
			          (outcome.matches == 1 ? " match" : " matches") +
			          " not yet struck through.";
			break;
		case NotebookStore::Status::Ambiguous:
			message = "Found " + std::to_string(outcome.matches) +
			          " matches; give occurrence 1-" +
			          std::to_string(outcome.matches) + ".";
			break;
		case NotebookStore::Status::NoRoom:
			message = "Not enough room on " + where + ": " +
			          std::to_string(outcome.roomLeft) + " characters left.";
			break;
		case NotebookStore::Status::EmptyText:
			message = "The text is empty.";
			break;
		case NotebookStore::Status::BadText:
			message = "The text is not valid.";
			break;
		case NotebookStore::Status::IoError:
			message = "The page could not be saved.";
			break;
	}
	NeuroSendActionResult(id.c_str(), false, message.c_str());
	return true;
}

void HandleOpen(std::string const &id, std::string const &data) {
	NotebookGate::Access const access = CurrentAccess();
	if (!access.Any()) {
		char const *why = "There is no notebook to open here.";
		if (access.reason == NotebookGate::Reason::NotYet) {
			why = "You do not have a notebook yet.";
		} else if (access.reason == NotebookGate::Reason::Lost) {
			why = "You lost your notebook.";
		}
		NeuroSendActionResult(id.c_str(), false, why);
		return;
	}
	int requested = access.first ? 1 : 2;
	if (data.find("\"notebook\"") != std::string::npos) {
		if (!ParseJsonInteger(data, "\"notebook\"", requested) ||
		    (requested != 1 && requested != 2)) {
			NeuroSendActionResult(id.c_str(), false, "\"notebook\" must be 1 or 2.");
			return;
		}
		if (!Has(access, requested)) {
			NeuroSendActionResult(
			    id.c_str(), false,
			    ("You do not have notebook " + std::to_string(requested) + " now.")
			        .c_str());
			return;
		}
	} else if (access.Both()) {
		NeuroSendActionResult(id.c_str(), false,
		                      "You have two notebooks; give \"notebook\": 1 for "
		                      "the first, 2 for the second.");
		return;
	}
	if (g_Open.load() != 0 && requested == Book()) {
		NeuroSendActionResult(id.c_str(), true,
		                      ("The notebook is already open on page " +
		                       std::to_string(g_CurrentPage) + ".")
		                          .c_str());
		return;
	}
	g_Book.store(requested);
	std::string const opened =
	    access.Both() ? "Opened notebook " + std::to_string(requested)
	                  : std::string("Opened your notebook");
	Store().RemoveLeftovers(Book());
	// Where she wrote last, so a note without a page carries on from there.
	int const last = Store().LatestWrittenPage(Book());
	g_CurrentPage = last > 0 ? last : 1;
	g_Open.store(1);
	std::string message =
	    last == 0 ? opened + ". It is blank."
	              : opened + ". Pages with writing: " +
	                    PageRanges(Store().WrittenPages(Book())) +
	                    ". It is open on page " + std::to_string(last) +
	                    ", the last one you wrote on";
	// After a break this is the first thing worth knowing.
	NotebookStore::PageTimes const times = Store().Times(Book(), g_CurrentPage);
	if (last != 0) {
		message += times.written ? ", " + times.modified + "." : ".";
	}
	NeuroSendActionResult(id.c_str(), true, message.c_str());
	// The rules, said once: the action descriptions stay a sentence each.
	if (!g_RulesSaid.exchange(true)) {
		std::string const rules =
		    "How your notebook works. It has " +
		    std::to_string(NotebookStore::kPageCount) + " pages of " +
		    std::to_string(NotebookStore::kPageCapacity) +
		    " characters; what is written stays. Without \"page\" an action "
		    "works on the page you are on, and with it moves you there. A note "
		    "too long for the page runs on to the next blank pages; line breaks "
		    "are kept, empty lines too. \"line\" in write_note must name a "
		    "blank line, which may be past the last one. A margin note is up "
		    "to " +
		    std::to_string(NotebookStore::kMarginNoteMax) +
		    " characters, one per line. read_page numbers the lines and shows "
		    "margin notes after a second bar. strike_text needs the text "
		    "exactly as written, and \"occurrence\" when it appears more than "
		    "once.";
		NeuroSendContext(rules.c_str(), true);
	}
}

void HandleRead(std::string const &id, std::string const &data) {
	int page = g_CurrentPage;
	bool valid = true;
	ReadPageArgument(data, page, valid);
	if (!valid) {
		NeuroSendActionResult(id.c_str(), false, PageRangeError().c_str());
		return;
	}
	std::string text;
	NotebookStore::Status const status = Store().ReadPage(Book(), page, text);
	std::string const name = "Page " + std::to_string(page);
	if (status == NotebookStore::Status::BadText) {
		NeuroSendActionResult(id.c_str(), false,
		                      (name + " is not readable text.").c_str());
		return;
	}
	if (status != NotebookStore::Status::Ok) {
		NeuroSendActionResult(id.c_str(), false,
		                      (name + " could not be read.").c_str());
		return;
	}
	g_CurrentPage = page;
	if (text.empty()) {
		NeuroSendActionResult(id.c_str(), true, (name + " is blank.").c_str());
		return;
	}
	std::string const shown = FormatPage(page, text);
	if (shown.size() <= kInlinePageBytes) {
		NeuroSendActionResult(id.c_str(), true, shown.c_str());
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      ("Reading page " + std::to_string(page) + ".").c_str());
	NeuroSendContext(("Your notebook. " + shown).c_str(), true);
}

void HandleWrite(std::string const &id, std::string const &data) {
	std::string text;
	if (!ParseJsonString(data, "\"text\"", text)) {
		NeuroSendActionResult(id.c_str(), false, "write_note requires \"text\".");
		return;
	}
	// Without a page: the one she is on, like every other action.
	int page = g_CurrentPage;
	bool valid = true;
	ReadPageArgument(data, page, valid);
	if (!valid) {
		NeuroSendActionResult(id.c_str(), false, PageRangeError().c_str());
		return;
	}
	int line = 0;
	ReadIntArgument(data, "\"line\"", line, valid);
	if (!valid) {
		NeuroSendActionResult(id.c_str(), false, "\"line\" must be 1 or more.");
		return;
	}
	if (line > 0) {
		NotebookStore::WriteOutcome const outcome =
		    Store().WriteLine(Book(), page, line, text);
		if (AnswerRefusal(id, outcome, page, line)) {
			return;
		}
		g_CurrentPage = page;
		NeuroSendActionResult(
		    id.c_str(), true,
		    ("Written on page " + std::to_string(page) + ", " +
		     LineSpan(outcome.firstLine, outcome.lastLine) + ". " +
		     std::to_string(outcome.roomLeft) + " characters left on that page.")
		        .c_str());
		return;
	}

	NotebookStore::WriteOutcome const outcome =
	    Store().Append(Book(), page, text);
	std::string const from = std::to_string(page);
	switch (outcome.status) {
		case NotebookStore::Status::Ok: {
			g_CurrentPage = outcome.lastPage;
			// With the lines, so a margin note can follow without reading the
			// page back.
			std::string message =
			    outcome.firstPage == outcome.lastPage
			        ? "Written on page " + std::to_string(outcome.lastPage) + ", " +
			              LineSpan(outcome.firstLine, outcome.lastLine) + "."
			        : "Written from page " + std::to_string(outcome.firstPage) +
			              " line " + std::to_string(outcome.firstLine) + " to page " +
			              std::to_string(outcome.lastPage) + " line " +
			              std::to_string(outcome.lastLine) + ".";
			message += " " + std::to_string(outcome.roomLeft) +
			           " characters left on that page.";
			NeuroSendActionResult(id.c_str(), true, message.c_str());
			return;
		}
		case NotebookStore::Status::NoRoom: {
			std::string message =
			    outcome.roomLeft == 0
			        ? "There is no room from page " + from +
			              ": the pages after it already have writing or the "
			              "notebook ends."
			        : "Too long: from page " + from + " there is room for " +
			              std::to_string(outcome.roomLeft) +
			              " characters before existing writing or the end of the "
			              "notebook.";
			int const blank = FirstBlankPage(Store().WrittenPages(Book()));
			if (blank != 0) {
				message += " Page " + std::to_string(blank) +
				           " is the first blank one; give \"page\" to write there.";
			}
			NeuroSendActionResult(id.c_str(), false, message.c_str());
			return;
		}
		case NotebookStore::Status::EmptyText:
			NeuroSendActionResult(id.c_str(), false, "The note is empty.");
			return;
		case NotebookStore::Status::BadText:
			NeuroSendActionResult(id.c_str(), false, "The note is not valid text.");
			return;
		case NotebookStore::Status::BadPage:
			NeuroSendActionResult(id.c_str(), false, PageRangeError().c_str());
			return;
		case NotebookStore::Status::IoError:
			NeuroSendActionResult(
			    id.c_str(), false,
			    outcome.lastPage > 0
			        ? ("The note stops at page " + std::to_string(outcome.lastPage) +
			           ": the rest could not be saved.")
			              .c_str()
			        : "The note could not be saved.");
			return;
		default:
			AnswerRefusal(id, outcome, page, 0);
			return;
	}
}

void HandleAnnotate(std::string const &id, std::string const &data) {
	std::string text;
	if (!ParseJsonString(data, "\"text\"", text)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "annotate_line requires \"text\".");
		return;
	}
	int line = 0;
	bool valid = true;
	if (!ReadIntArgument(data, "\"line\"", line, valid) || !valid) {
		NeuroSendActionResult(id.c_str(), false,
		                      "annotate_line requires \"line\", 1 or more.");
		return;
	}
	int page = g_CurrentPage;
	ReadPageArgument(data, page, valid);
	if (!valid) {
		NeuroSendActionResult(id.c_str(), false, PageRangeError().c_str());
		return;
	}
	NotebookStore::WriteOutcome const outcome =
	    Store().Annotate(Book(), page, line, text);
	if (AnswerRefusal(id, outcome, page, line)) {
		return;
	}
	g_CurrentPage = page;
	NeuroSendActionResult(id.c_str(), true,
	                      ("Noted beside line " + std::to_string(line) +
	                       " of page " + std::to_string(page) + ".")
	                          .c_str());
}

void HandleStrike(std::string const &id, std::string const &data) {
	std::string quote;
	if (!ParseJsonString(data, "\"quote\"", quote)) {
		NeuroSendActionResult(id.c_str(), false, "strike_text requires \"quote\".");
		return;
	}
	bool valid = true;
	int page = g_CurrentPage;
	ReadPageArgument(data, page, valid);
	if (!valid) {
		NeuroSendActionResult(id.c_str(), false, PageRangeError().c_str());
		return;
	}
	int occurrence = 0;
	ReadIntArgument(data, "\"occurrence\"", occurrence, valid);
	if (!valid) {
		NeuroSendActionResult(id.c_str(), false,
		                      "\"occurrence\" must be 1 or more.");
		return;
	}
	NotebookStore::WriteOutcome const outcome =
	    Store().Strike(Book(), page, quote, occurrence);
	if (AnswerRefusal(id, outcome, page, 0)) {
		return;
	}
	g_CurrentPage = page;
	NeuroSendActionResult(id.c_str(), true,
	                      ("Struck through \"" + Excerpt(quote) + "\" on page " +
	                       std::to_string(page) + ".")
	                          .c_str());
}

// Story news about the notebook, into the log as well: it is the only trace of
// when the story took one away or gave one back.
void Tell(char const *text, bool silent) {
	Log((std::string("[Notebook] ") + text + "\n").c_str());
	NeuroSendContext(text, silent);
}

}  // namespace

namespace ScreenNotebook {

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	bool const ours = name == "open_notebook" || name == "read_page" ||
	                  name == "write_note" || name == "annotate_line" ||
	                  name == "strike_text" || name == "close_notebook";
	if (!ours || !GetPluginConfig().Notebook) {
		return false;
	}
	std::lock_guard<std::mutex> lock(g_Mutex);
	if (name == "open_notebook") {
		HandleOpen(id, data);
		return true;
	}
	if (g_Open.load() == 0) {
		if (name == "close_notebook") {
			NeuroSendActionResult(id.c_str(), true,
			                      "The notebook is already closed.");
		} else {
			NeuroSendActionResult(id.c_str(), false,
			                      "Your notebook is closed. Open it first.");
		}
		return true;
	}
	if (name == "read_page") {
		HandleRead(id, data);
	} else if (name == "write_note") {
		HandleWrite(id, data);
	} else if (name == "annotate_line") {
		HandleAnnotate(id, data);
	} else if (name == "strike_text") {
		HandleStrike(id, data);
	} else {
		g_Open.store(0);
		NeuroSendActionResult(id.c_str(), true, "Closed the notebook.");
	}
	return true;
}

void AppendActions(ActionSet &set, bool quickMenuOut) {
	if (!GetPluginConfig().Notebook) {
		return;
	}
	if (g_Open.load() == 0) {
		// The notebook is taken out from the quick menu, like its buttons.
		if (!quickMenuOut) {
			return;
		}
		// No notebook to choose while there is only one: a number offered
		// here only invites wrong ones.
		if (CurrentAccess().Both()) {
			set.add("open_notebook",
			        "Open one of your notebooks to read or write in it: 1 is "
			        "the first, 2 is the one bought after the first was lost.",
			        "{\"type\":\"object\",\"properties\":{\"notebook\":"
			        "{\"type\":\"integer\",\"enum\":[1,2]}},"
			        "\"required\":[\"notebook\"],\"additionalProperties\":false}");
		} else {
			set.add("open_notebook", kOpenDescription,
			        "{\"type\":\"object\",\"properties\":{},"
			        "\"additionalProperties\":false}");
		}
		return;
	}
	std::string const pages = std::to_string(NotebookStore::kPageCount);
	std::string const capacity = std::to_string(NotebookStore::kPageCapacity);
	std::string const pageField =
	    "\"page\":{\"type\":\"integer\",\"minimum\":1,"
	    "\"maximum\":" +
	    pages + "}";
	std::string const lineField = "\"line\":{\"type\":\"integer\",\"minimum\":1}";
	set.add("read_page",
	        "Read a page of your notebook; without \"page\", the one you are on.",
	        set.keep("{\"type\":\"object\",\"properties\":{" + pageField +
	                 "},\"additionalProperties\":false}"));
	set.add("write_note",
	        "Write in your notebook, at the end of the page you are on or of "
	        "\"page\". With \"line\", on that blank line of the page instead.",
	        set.keep("{\"type\":\"object\",\"properties\":{\"text\":"
	                 "{\"type\":\"string\",\"minLength\":1,\"maxLength\":" +
	                 capacity + "}," + pageField + "," + lineField +
	                 "},\"required\":[\"text\"],"
	                 "\"additionalProperties\":false}"));
	std::string const noteMax = std::to_string(NotebookStore::kMarginNoteMax);
	set.add("annotate_line",
	        "Add a short note in the margin beside a line of the page.",
	        set.keep("{\"type\":\"object\",\"properties\":{\"text\":"
	                 "{\"type\":\"string\",\"minLength\":1,\"maxLength\":" +
	                 noteMax + "}," + lineField + "," + pageField +
	                 "},\"required\":[\"text\",\"line\"],"
	                 "\"additionalProperties\":false}"));
	set.add("strike_text",
	        "Strike through text on the page, quoted as it is written; it stays "
	        "readable.",
	        set.keep("{\"type\":\"object\",\"properties\":{\"quote\":"
	                 "{\"type\":\"string\",\"minLength\":1},"
	                 "\"occurrence\":{\"type\":\"integer\",\"minimum\":1}," +
	                 pageField +
	                 "},\"required\":[\"quote\"],"
	                 "\"additionalProperties\":false}"));
	set.add("close_notebook", "Close your notebook.",
	        "{\"type\":\"object\",\"properties\":{},"
	        "\"additionalProperties\":false}");
}

std::string ReferenceLine() {
	return GetPluginConfig().Notebook
	           ? std::string("open_notebook -- ") + kOpenDescription
	           : std::string();
}

int StateKey() {
	return g_Open.load() | (CurrentAccess().Both() ? 2 : 0);
}

void NoteMonitorOutput(std::string const &output) {
	// Main thread only. The monitor reports a field when it changes, so the
	// last scene and line are kept between calls.
	static std::string scenario;
	static int line = 0;
	static bool replay = false;
	bool changed = false;
	size_t start = 0;
	while (start < output.size()) {
		size_t end = output.find('\n', start);
		if (end == std::string::npos) {
			end = output.size();
		}
		std::string const field = output.substr(start, end - start);
		start = end + 1;
		if (field.compare(0, 11, "[scenario] ") == 0) {
			scenario = field.substr(11);
			line = 0;
			changed = true;
		} else if (field.compare(0, 7, "[line] ") == 0) {
			// Line 0 is the conductor between places -- a scene ending, a choice
			// being taken -- not a place in the scene.
			int const reported = atoi(field.c_str() + 7);
			if (reported != 0) {
				line = reported;
				changed = true;
			}
		} else if (field.compare(0, 12, "[recollect] ") == 0) {
			replay = atoi(field.c_str() + 12) == 1;
			changed = true;
		} else if (field.compare(0, 10, "[restore] ") == 0) {
			// Counted by the engine's restore hook; the first report is the
			// standing count, zero, and not a jump.
			if (atoi(field.c_str() + 10) != 0) {
				g_Jumps.fetch_add(1);
			}
		}
	}
	if (!changed) {
		return;
	}
	NotebookGate::Access access = NotebookGate::At(scenario, line);
	// A menu or the title runs a scenario of its own behind the story; the
	// story has not moved, so what was known stands.
	if (access.reason == NotebookGate::Reason::NotInStory) {
		return;
	}
	if (replay) {
		access = NotebookGate::Access();
	} else if (NotebookGate::KeepsNotebooks(scenario)) {
		g_UnlockPending.fetch_or(Pack(access) & (kAccessFirst | kAccessSecond));
	}
	// The engine's restore hook does not see every jump: going back to a choice
	// point leaves no mark but the position. Text only runs forward, so a line
	// that went back, or a scene that is not the one this leads to, is a jump.
	static int lastScene = -1;
	static int lastLine = 0;
	int const scene = NotebookGate::SceneIndex(scenario);
	if (lastScene >= 0 &&
	    (scene == lastScene ? line < lastLine
	                        : !NotebookGate::FollowsOn(lastScene, scene))) {
		g_Jumps.fetch_add(1);
	} else if ((scene != lastScene || line - lastLine > kJumpStride) &&
	           (g_JumpAsked.load() ||
	            GetTickCount64() < g_JumpAnswerUntil.load())) {
		// Far forward in one step, while a jump is being asked about or just
		// after: it was agreed to. The position can move before the dialog is
		// seen to be gone. Reading on, even skipping, moves a few lines between
		// reports.
		g_JumpAnswerUntil.store(0);
		g_Jumps.fetch_add(1);
	}
	lastScene = scene;
	lastLine = line;
	g_StoryScene.store(scene);
	g_StoryAccess.store(Pack(access));
}

void NoteJump() {
	g_Jumps.fetch_add(1);
}

void NoteJumpAsked(bool asked) {
	g_JumpAsked.store(asked);
}

void NoteJumpDialogClosed() {
	if (g_JumpAsked.exchange(false)) {
		g_JumpAnswerUntil.store(GetTickCount64() + kJumpAnswerMs);
	}
}

int UnlockToSave() {
	int const pending = g_UnlockPending.load();
	int const kept = g_Unlocked.load();
	return (pending & ~kept) != 0 ? (pending | kept) : 0;
}

void SetUnlocked(int books) {
	g_Unlocked.store(books & (kAccessFirst | kAccessSecond));
	// What an ending earned before is either saved by now or being replaced.
	g_UnlockPending.store(0);
}

void SetAsideNotebooks() {
	// The folder is renamed, never deleted: what is in it may be what she
	// wrote on stream. Off the main thread, because it waits on the disk.
	std::thread([] {
		std::lock_guard<std::mutex> lock(g_Mutex);
		std::wstring const root = NotebookRoot();
		if (GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES) {
			return;
		}
		g_Open.store(0);
		SYSTEMTIME now = {};
		GetLocalTime(&now);
		wchar_t stamp[32] = {};
		swprintf(stamp, 32, L"-%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth,
		         now.wDay, now.wHour, now.wMinute, now.wSecond);
		// A scanner or an open Explorer window can hold the folder for a moment.
		bool moved = false;
		for (int attempt = 0; attempt < 10 && !moved; ++attempt) {
			moved = MoveFileW(root.c_str(), (root + stamp).c_str()) != 0;
			if (!moved) {
				Sleep(300);
			}
		}
		DWORD const error = moved ? 0 : GetLastError();
		Log(moved ? "[Notebook] new profile: earlier notebooks set aside\n"
		          : ("[Notebook] new profile: could not set the notebooks "
		             "aside, error " +
		             std::to_string(error) + "\n")
		                .c_str());
	}).detach();
}

void FollowStory() {
	if (!GetPluginConfig().Notebook || !GetPluginConfig().NotebookStoryGate) {
		return;
	}
	// A save loaded or a backlog jump puts the notebook away, wherever it lands.
	int const jumps = g_Jumps.load();
	if (g_FollowedJumps.exchange(jumps) != jumps) {
		g_QuietUntil.store(GetTickCount64() + kJumpSettleMs);
		if (g_Open.exchange(0) != 0) {
			Tell("Your notebook was closed: the story moved somewhere else.", true);
		}
	}
	int const scene = g_StoryScene.load();
	bool const sameScene = g_FollowedScene.exchange(scene) == scene;
	int const now = Effective(g_StoryAccess.load());
	int const before = g_FollowedAccess.exchange(now);
	if (now == before || now == kAccessUnknown) {
		return;
	}
	NotebookGate::Access const access = Unpack(now);
	if (g_Open.load() != 0 && !Has(access, Book())) {
		g_Open.store(0);
		Tell("Your notebook was closed: you no longer have it.", true);
	}
	// Where this run starts is not news, nor is where a jump lands; only what
	// changes while she watches.
	if (before == kAccessUnknown || !sameScene ||
	    GetTickCount64() < g_QuietUntil.load()) {
		return;
	}
	NotebookGate::Access const was = Unpack(before);
	if (was.reason == NotebookGate::Reason::NotInStory ||
	    access.reason == NotebookGate::Reason::NotInStory) {
		return;
	}
	if (!was.Any() && access.Any()) {
		Tell(
		    "You've got a notebook! open_notebook, in the quick "
		    "menu, opens it to read or write.",
		    false);
	} else if (was.Any() && !access.Any()) {
		// The story says so itself, and there is nothing for her to do about it.
		Tell("You lost your notebook.", true);
	} else if (!was.Both() && access.Both()) {
		Tell(
		    "You have two notebooks now: open_notebook takes "
		    "\"notebook\", 1 for the first or 2 for the second.",
		    false);
	}
}

void Close() {
	std::lock_guard<std::mutex> lock(g_Mutex);
	if (g_Open.exchange(0) != 0) {
		NeuroSendContext("Your notebook was closed.", true);
	}
}

}  // namespace ScreenNotebook
