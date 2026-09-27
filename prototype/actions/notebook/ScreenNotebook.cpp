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

namespace {

// Only one notebook exists until the story decides which are in hand.
constexpr int kBook = 1;
// A page up to this size comes back in the result itself; a longer one goes
// to context, and the result only says which page is being read.
constexpr size_t kInlinePageBytes = 300;

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
	int const last = Store().LastWrittenPage(kBook);
	g_CurrentPage = last > 0 ? last : 1;
	g_Open.store(1);
	std::string const message =
	    last == 0 ? "Opened your notebook. It is blank."
	              : "Opened your notebook. " + std::to_string(last) + " of " +
	                    std::to_string(NotebookStore::kPageCount) +
	                    " pages have writing; it is open on page " +
	                    std::to_string(last) + ".";
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
	if (text.size() <= kInlinePageBytes) {
		NeuroSendActionResult(id.c_str(), true, (name + ":\n" + text).c_str());
		return;
	}
	NeuroSendActionResult(id.c_str(), true,
	                      ("Reading page " + std::to_string(page) + ".").c_str());
	NeuroSendContext(
	    ("Your notebook, page " + std::to_string(page) + ":\n\n" + text).c_str(),
	    true);
}

void HandleWrite(std::string const &id, std::string const &data) {
	std::string text;
	if (!ParseJsonString(data, "\"text\"", text)) {
		NeuroSendActionResult(id.c_str(), false, "write_note requires \"text\".");
		return;
	}
	// Without a page the note goes after the last one written.
	int const last = Store().LastWrittenPage(kBook);
	int page = last > 0 ? last : 1;
	bool valid = true;
	ReadPageArgument(data, page, valid);
	if (!valid) {
		NeuroSendActionResult(id.c_str(), false, PageRangeError().c_str());
		return;
	}

	NotebookStore::WriteOutcome const outcome = Store().Append(kBook, page, text);
	std::string const from = std::to_string(page);
	switch (outcome.status) {
		case NotebookStore::Status::Ok: {
			g_CurrentPage = outcome.lastPage;
			std::string message =
			    outcome.firstPage == outcome.lastPage
			        ? "Written on page " + std::to_string(outcome.lastPage) + "."
			        : "Written on pages " + std::to_string(outcome.firstPage) +
			              " to " + std::to_string(outcome.lastPage) + ".";
			message += " " + std::to_string(outcome.roomLeft) +
			           " characters left on that page.";
			NeuroSendActionResult(id.c_str(), true, message.c_str());
			return;
		}
		case NotebookStore::Status::NoRoom:
			NeuroSendActionResult(
			    id.c_str(), false,
			    outcome.roomLeft == 0
			        ? ("There is no room from page " + from +
			           ": the pages after it already have writing or the "
			           "notebook ends.")
			              .c_str()
			        : ("Too long: from page " + from + " there is room for " +
			           std::to_string(outcome.roomLeft) +
			           " characters before existing writing or the end of the "
			           "notebook.")
			              .c_str());
			return;
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
	}
}

}  // namespace

namespace ScreenNotebook {

bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data) {
	bool const ours = name == "open_notebook" || name == "read_page" ||
	                  name == "write_note" || name == "close_notebook";
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
	} else {
		g_Open.store(0);
		NeuroSendActionResult(id.c_str(), true, "Closed the notebook.");
	}
	return true;
}

void AppendActions(ActionSet &set) {
	if (!GetPluginConfig().Notebook) {
		return;
	}
	if (g_Open.load() == 0) {
		set.add("open_notebook", "Open your notebook to read or write in it.",
		        "{\"type\":\"object\",\"properties\":{\"notebook\":"
		        "{\"type\":\"integer\",\"minimum\":1}},"
		        "\"additionalProperties\":false}");
		return;
	}
	std::string const pages = std::to_string(NotebookStore::kPageCount);
	std::string const capacity = std::to_string(NotebookStore::kPageCapacity);
	set.add("read_page",
	        set.keep("Read a page of your notebook (" + pages +
	                 " pages). Without \"page\", reads the page you are on."),
	        set.keep("{\"type\":\"object\",\"properties\":{\"page\":"
	                 "{\"type\":\"integer\",\"minimum\":1,\"maximum\":" +
	                 pages + "}},\"additionalProperties\":false}"));
	set.add("write_note",
	        set.keep("Write a note in your notebook. It goes after your last "
	                 "note unless \"page\" is given; a page holds " +
	                 capacity +
	                 " characters, and a longer note continues on the next "
	                 "blank pages."),
	        set.keep("{\"type\":\"object\",\"properties\":{\"text\":"
	                 "{\"type\":\"string\",\"minLength\":1,\"maxLength\":" +
	                 capacity +
	                 "},\"page\":{\"type\":\"integer\",\"minimum\":1,"
	                 "\"maximum\":" +
	                 pages +
	                 "}},\"required\":[\"text\"],"
	                 "\"additionalProperties\":false}"));
	set.add("close_notebook", "Close your notebook.",
	        "{\"type\":\"object\",\"properties\":{},"
	        "\"additionalProperties\":false}");
}

int StateKey() {
	return g_Open.load();
}

void Close() {
	std::lock_guard<std::mutex> lock(g_Mutex);
	g_Open.store(0);
}

}  // namespace ScreenNotebook
