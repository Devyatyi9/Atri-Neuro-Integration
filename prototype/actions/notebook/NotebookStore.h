#pragma once

#include <mutex>
#include <string>
#include <vector>

// Neuro's notebooks on disk: one UTF-8 file per page, always replaced whole.
//
// Knows nothing about Neuro or the game. It waits on the disk, including short
// sleeps while a scanner holds a file, so it must never run on the game's main
// thread.
class NotebookStore {
 public:
	static constexpr int kPageCount = 100;
	// Characters, not bytes.
	static constexpr int kPageCapacity = 1665;
	// A margin note is a few words beside a line, not another entry.
	static constexpr int kMarginNoteMax = 30;
	// Where reading lines up the margin: the width of a line on the page.
	static constexpr int kMarginColumn = 37;

	enum class Status {
		Ok,
		BadPage,
		// Not valid UTF-8, on the way in or in a page edited by hand.
		BadText,
		EmptyText,
		NoRoom,
		IoError,
		// No such line on the page.
		BadLine,
		// Writing on a line that already has writing.
		LineTaken,
		// More lines of text than blank lines in a row from the one given.
		NotEnoughLines,
		// A second margin note on the same line.
		HasNote,
		// A margin note longer than kMarginNoteMax.
		TooLong,
		// Text to strike: not on the page, or only inside struck text.
		NotFound,
		// Text to strike occurs more than once and no occurrence was given.
		Ambiguous,
	};

	struct WriteOutcome {
		Status status = Status::IoError;
		// Pages that received text; 0 when none did.
		int firstPage = 0;
		int lastPage = 0;
		// Characters still free on lastPage. With NoRoom: how many would fit
		// starting from the requested page.
		int roomLeft = 0;
		// Strike: how many places the text was found.
		int matches = 0;
		// NotEnoughLines: blank lines in a row from the one given; with it,
		// how many lines the text has.
		int blankLines = 0;
		int textLines = 0;
	};

	struct PageTimes {
		bool written = false;
		// Local time, "2026-10-01 14:32".
		std::string created;
		std::string modified;
	};

	explicit NotebookStore(std::wstring root,
	                       int pageCount = kPageCount,
	                       int pageCapacity = kPageCapacity);

	// A page never written reads as empty, not as an error.
	Status ReadPage(int book, int page, std::string &utf8);

	// The last page with anything on it, or 0 for a blank notebook.
	int LastWrittenPage(int book);

	// Adds the note after what the page holds. What does not fit continues on
	// the following pages, which must be blank: a note is never threaded
	// between existing writing, and nothing is written when it would have to.
	WriteOutcome Append(int book, int page, std::string const &utf8);

	// Writes on a blank line of the page (1-based); a margin note already on it
	// stays. Text with line breaks fills that many blank lines in a row and is
	// refused when they are not there: nothing written ever moves. Nothing
	// continues onto the next page.
	WriteOutcome WriteLine(int book, int page, int line, std::string const &utf8);

	// Adds a margin note to a line, blank lines included. One per line.
	WriteOutcome Annotate(int book, int page, int line, std::string const &utf8);

	// Strikes through the text where it is found, leaving it readable.
	// occurrence is 1-based; 0 accepts only a single match.
	WriteOutcome Strike(int book,
	                    int page,
	                    std::string const &quote,
	                    int occurrence);

	// When the page was first and last written, from the file itself.
	PageTimes Times(int book, int page);

	// Temporary files left by a write that never finished.
	void RemoveLeftovers(int book);

	int PageCount() const { return m_PageCount; }
	int PageCapacity() const { return m_PageCapacity; }

	// Code points: a surrogate pair is one character.
	static int CountCharacters(std::wstring const &text);

	// What the page's capacity counts: note marks and strike-through marks
	// take no room.
	static int CountWriting(std::wstring const &text);

	// The page as Neuro reads it: numbered lines, margin notes lined up after
	// a bar, the note marks gone.
	static std::wstring NumberedLines(std::wstring const &text);

	// Cuts text into page-sized pieces. The first piece gets `room` characters,
	// every further one `capacity`. Cuts fall on whitespace where there is any;
	// a first piece that cannot end on a word after existing writing comes
	// back empty, meaning the note starts on the next page instead.
	static std::vector<std::wstring> SplitForPages(std::wstring const &text,
	                                               int room,
	                                               int capacity);

 private:
	std::wstring BookFolder(int book) const;
	std::wstring PagePath(int book, int page) const;
	bool ValidPage(int book, int page) const;
	Status ReadPageLocked(int book, int page, std::wstring &text);
	Status SavePageLocked(int book, int page, std::wstring const &text);

	std::wstring m_Root;
	int m_PageCount;
	int m_PageCapacity;
	std::mutex m_Mutex;
};
