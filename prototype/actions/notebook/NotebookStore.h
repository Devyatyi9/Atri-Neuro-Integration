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

	enum class Status {
		Ok,
		BadPage,
		// Not valid UTF-8, on the way in or in a page edited by hand.
		BadText,
		EmptyText,
		NoRoom,
		IoError,
	};

	struct WriteOutcome {
		Status status = Status::IoError;
		// Pages that received text; 0 when none did.
		int firstPage = 0;
		int lastPage = 0;
		// Characters still free on lastPage. With NoRoom: how many would fit
		// starting from the requested page.
		int roomLeft = 0;
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

	// Temporary files left by a write that never finished.
	void RemoveLeftovers(int book);

	int PageCount() const { return m_PageCount; }
	int PageCapacity() const { return m_PageCapacity; }

	// Code points: a surrogate pair is one character.
	static int CountCharacters(std::wstring const &text);

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

	std::wstring m_Root;
	int m_PageCount;
	int m_PageCapacity;
	std::mutex m_Mutex;
};
