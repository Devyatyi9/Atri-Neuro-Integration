#include "ScreenNotebook.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

#include "ActionJson.h"
#include "ActionSet.h"
#include "NeuroClient.h"
#include "NotebookStore.h"
#include "PluginConfig.h"
#include "UtfConversion.h"

namespace {

// Only one notebook exists until the story decides which are in hand.
constexpr int kBook = 1;
// A page up to this size comes back in the result itself; a longer one goes
// to context, and the result only says which page is being read.
constexpr size_t kInlinePageBytes = 300;

char const *const kOpenDescription =
    "Open your notebook to read or write in it.";

std::mutex g_Mutex;
std::atomic<int> g_Open(0);
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
	NotebookStore::PageTimes const times = Store().Times(kBook, page);
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
	int requested = kBook;
	if (data.find("\"notebook\"") != std::string::npos &&
	    (!ParseJsonInteger(data, "\"notebook\"", requested) ||
	     requested != kBook)) {
		NeuroSendActionResult(id.c_str(), false, "Only notebook 1 is available.");
		return;
	}
	if (g_Open.load() != 0) {
		NeuroSendActionResult(id.c_str(), true,
		                      ("The notebook is already open on page " +
		                       std::to_string(g_CurrentPage) + ".")
		                          .c_str());
		return;
	}
	Store().RemoveLeftovers(kBook);
	// Where she wrote last, so a note without a page carries on from there.
	int const last = Store().LatestWrittenPage(kBook);
	g_CurrentPage = last > 0 ? last : 1;
	g_Open.store(1);
	std::string message =
	    last == 0 ? "Opened your notebook. It is blank."
	              : "Opened your notebook. Pages with writing: " +
	                    PageRanges(Store().WrittenPages(kBook)) +
	                    ". It is open on page " + std::to_string(last) +
	                    ", the last one you wrote on";
	// After a break this is the first thing worth knowing.
	NotebookStore::PageTimes const times = Store().Times(kBook, g_CurrentPage);
	if (last != 0) {
		message += times.written ? ", " + times.modified + "." : ".";
	}
	NeuroSendActionResult(id.c_str(), true, message.c_str());
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
	NotebookStore::Status const status = Store().ReadPage(kBook, page, text);
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
		    Store().WriteLine(kBook, page, line, text);
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

	NotebookStore::WriteOutcome const outcome = Store().Append(kBook, page, text);
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
			int const blank = FirstBlankPage(Store().WrittenPages(kBook));
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
	    Store().Annotate(kBook, page, line, text);
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
	    Store().Strike(kBook, page, quote, occurrence);
	if (AnswerRefusal(id, outcome, page, 0)) {
		return;
	}
	g_CurrentPage = page;
	NeuroSendActionResult(id.c_str(), true,
	                      ("Struck through \"" + Excerpt(quote) + "\" on page " +
	                       std::to_string(page) + ".")
	                          .c_str());
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
		set.add("open_notebook", kOpenDescription,
		        "{\"type\":\"object\",\"properties\":{},"
		        "\"additionalProperties\":false}");
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
	        set.keep("Read a page of your notebook (" + pages +
	                 " pages). Without \"page\", reads the page you are on. "
	                 "Lines come numbered, margin notes after the second bar."),
	        set.keep("{\"type\":\"object\",\"properties\":{" + pageField +
	                 "},\"additionalProperties\":false}"));
	set.add(
	    "write_note",
	    set.keep("Write in your notebook; what is written stays. Without "
	             "\"page\", it goes at the end of the page you are on; a page "
	             "holds " +
	             capacity +
	             " characters, and a longer note continues on the next "
	             "blank pages. Line breaks in the text are kept, empty lines "
	             "too. With \"line\", writes on that line of the page instead: "
	             "it must be blank, and may be past the last line."),
	    set.keep("{\"type\":\"object\",\"properties\":{\"text\":"
	             "{\"type\":\"string\",\"minLength\":1,\"maxLength\":" +
	             capacity + "}," + pageField + "," + lineField +
	             "},\"required\":[\"text\"],"
	             "\"additionalProperties\":false}"));
	std::string const noteMax = std::to_string(NotebookStore::kMarginNoteMax);
	set.add("annotate_line",
	        set.keep("Add a short note in the margin beside a line, up to " +
	                 noteMax +
	                 " characters; one per line, blank lines and lines past the "
	                 "last one too. Without "
	                 "\"page\", on the page you are on."),
	        set.keep("{\"type\":\"object\",\"properties\":{\"text\":"
	                 "{\"type\":\"string\",\"minLength\":1,\"maxLength\":" +
	                 noteMax + "}," + lineField + "," + pageField +
	                 "},\"required\":[\"text\",\"line\"],"
	                 "\"additionalProperties\":false}"));
	set.add("strike_text",
	        set.keep(std::string(
	            "Strike through text on a page; it stays readable. Give it "
	            "exactly as written. If it appears more than once, choose "
	            "which with \"occurrence\". Without \"page\", on the page you "
	            "are on.")),
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
	return g_Open.load();
}

void Close() {
	std::lock_guard<std::mutex> lock(g_Mutex);
	if (g_Open.exchange(0) != 0) {
		NeuroSendContext("Your notebook was closed.", true);
	}
}

}  // namespace ScreenNotebook
