#include "NotebookStore.h"

#include <windows.h>

#include <string>
#include <vector>

#include "TestMain.h"

namespace {

using Status = NotebookStore::Status;

std::wstring MakeTempRoot() {
	wchar_t base[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, base);
	return std::wstring(base) + L"notebook-tests-" +
	       std::to_wstring(GetCurrentProcessId()) + L"-" +
	       std::to_wstring(GetTickCount());
}

void RemoveTree(std::wstring const &folder) {
	WIN32_FIND_DATAW found = {};
	HANDLE const search = FindFirstFileW((folder + L"\\*").c_str(), &found);
	if (search != INVALID_HANDLE_VALUE) {
		do {
			std::wstring const name = found.cFileName;
			if (name == L"." || name == L"..") {
				continue;
			}
			std::wstring const path = folder + L"\\" + name;
			if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				RemoveTree(path);
			} else {
				DeleteFileW(path.c_str());
			}
		} while (FindNextFileW(search, &found));
		FindClose(search);
	}
	RemoveDirectoryW(folder.c_str());
}

void WriteRaw(std::wstring const &path, std::string const &bytes) {
	HANDLE const file =
	    CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
	                FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
	CloseHandle(file);
}

bool Exists(std::wstring const &path) {
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string Read(NotebookStore &store, int book, int page) {
	std::string text;
	store.ReadPage(book, page, text);
	return text;
}

void SplitTests() {
	// Surrogate pair counts once.
	CHECK(NotebookStore::CountCharacters(L"ab\xD83D\xDE00") == 3);

	std::vector<std::wstring> pieces =
	    NotebookStore::SplitForPages(L"short note", 20, 20);
	CHECK(pieces.size() == 1 && pieces[0] == L"short note");

	pieces = NotebookStore::SplitForPages(L"aaa bbb ccc ddd", 8, 8);
	CHECK(pieces.size() == 2 && pieces[0] == L"aaa bbb" &&
	      pieces[1] == L"ccc ddd");

	// No room left on the page: the note starts on the next one.
	pieces = NotebookStore::SplitForPages(L"word", 0, 10);
	CHECK(pieces.size() == 2 && pieces[0].empty() && pieces[1] == L"word");

	// First word does not fit in the room left.
	pieces = NotebookStore::SplitForPages(L"longword tail", 3, 10);
	CHECK(pieces.size() == 3 && pieces[0].empty() && pieces[1] == L"longword" &&
	      pieces[2] == L"tail");

	// A word longer than a whole page is cut where the page ends.
	pieces = NotebookStore::SplitForPages(L"abcdefghij", 4, 4);
	CHECK(pieces.size() == 3 && pieces[0] == L"abcd" && pieces[1] == L"efgh" &&
	      pieces[2] == L"ij");

	// Never splits a surrogate pair.
	pieces = NotebookStore::SplitForPages(L"a\xD83D\xDE00zz", 2, 2);
	CHECK(pieces.size() == 2 && pieces[0] == L"a\xD83D\xDE00" &&
	      pieces[1] == L"zz");
}

void StoreTests() {
	std::wstring const root = MakeTempRoot();
	{
		NotebookStore store(root, 5, 20);
		std::wstring const folder = root + L"\\book1";

		CHECK(store.LastWrittenPage(1) == 0);
		CHECK(Read(store, 1, 1).empty());

		std::string text;
		CHECK(store.ReadPage(1, 0, text) == Status::BadPage);
		CHECK(store.ReadPage(1, 6, text) == Status::BadPage);
		CHECK(store.Append(0, 1, "x").status == Status::BadPage);
		CHECK(store.Append(1, 1, "  \r\n ").status == Status::EmptyText);
		CHECK(store.Append(1, 1, std::string("\xC3\x28")).status ==
		      Status::BadText);

		NotebookStore::WriteOutcome outcome = store.Append(1, 1, "first note");
		CHECK(outcome.status == Status::Ok && outcome.firstPage == 1 &&
		      outcome.lastPage == 1 && outcome.roomLeft == 10);
		CHECK(Read(store, 1, 1) == "first note");
		CHECK(store.LastWrittenPage(1) == 1);

		// Second note on its own line; CR is dropped.
		outcome = store.Append(1, 1, "two\r\nlines");
		CHECK(outcome.status == Status::Ok);
		CHECK(Read(store, 1, 1) == "first note\ntwo\nlines");

		// Overflow continues on the blank page after.
		outcome = store.Append(1, 1, "overflow into next");
		CHECK(outcome.status == Status::Ok && outcome.firstPage == 2 &&
		      outcome.lastPage == 2);
		CHECK(Read(store, 1, 2) == "overflow into next");

		// Next page has writing: refused, nothing changes.
		std::string const before = Read(store, 1, 1);
		CHECK(store.Append(1, 3, "page three").status == Status::Ok);
		outcome = store.Append(1, 2, "this needs another page to fit");
		CHECK(outcome.status == Status::NoRoom);
		CHECK(Read(store, 1, 1) == before);
		CHECK(Read(store, 1, 2) == "overflow into next");

		// Past the last page.
		outcome = store.Append(1, 5, "this one is far too long for one page");
		CHECK(outcome.status == Status::NoRoom);
		CHECK(store.LastWrittenPage(1) == 3);

		// A page saved by an editor: BOM and CRLF.
		WriteRaw(folder + L"\\page_004.txt",
		         std::string("\xEF\xBB\xBFhand\r\nedit"));
		CHECK(Read(store, 1, 4) == "hand\nedit");

		// Not UTF-8 on disk.
		WriteRaw(folder + L"\\page_005.txt", std::string("\xC3\x28"));
		CHECK(store.ReadPage(1, 5, text) == Status::BadText);

		// Leftovers from an interrupted write.
		WriteRaw(folder + L"\\page_001.txt.tmp", "partial");
		store.RemoveLeftovers(1);
		CHECK(!Exists(folder + L"\\page_001.txt.tmp"));
		CHECK(Exists(folder + L"\\page_001.txt"));

		// Books are separate.
		CHECK(store.LastWrittenPage(2) == 0);
		CHECK(store.Append(2, 1, "other book").status == Status::Ok);
		CHECK(Read(store, 1, 1) == before);
	}
	RemoveTree(root);
}

void CountAndFormatTests() {
	// Strike marks and the note mark take no room.
	CHECK(NotebookStore::CountWriting(L"a ~~bc~~ d") == 6);
	CHECK(NotebookStore::CountWriting(L"x | y\x1F") == 5);
	// An unclosed "~~" is writing like any other.
	CHECK(NotebookStore::CountWriting(L"a ~~b") == 5);

	// Numbers on the left, margin notes lined up, the mark hidden, a blank
	// line kept.
	std::wstring const shown = NotebookStore::NumberedLines(
	    L"Met Atri.\n| train was late?\x1F\nNatsuki. | again\x1F");
	std::wstring const blank(NotebookStore::kMarginColumn, L' ');
	CHECK(shown == L"1 | Met Atri.\n2 | " + blank + L" | train was late?\n3 | " +
	                   L"Natsuki." +
	                   std::wstring(NotebookStore::kMarginColumn - 8, L' ') +
	                   L" | again");
	CHECK(NotebookStore::NumberedLines(L"").empty());
}

void LineTests() {
	std::wstring const root = MakeTempRoot();
	{
		NotebookStore store(root, 3, 60);

		// Line breaks and blank lines in a note are kept.
		CHECK(store.Append(1, 1, "one\n\nthree").status == Status::Ok);
		CHECK(Read(store, 1, 1) == "one\n\nthree");

		// Margin notes: one per line, blank lines too, length capped.
		CHECK(store.Annotate(1, 1, 1, "first").status == Status::Ok);
		CHECK(store.Annotate(1, 1, 1, "again").status == Status::HasNote);
		CHECK(store.Annotate(1, 1, 2, "on a blank line").status == Status::Ok);
		CHECK(store.Annotate(1, 1, 4, "no such line").status == Status::BadLine);
		CHECK(store.Annotate(1, 1, 3, std::string(31, 'x')).status ==
		      Status::TooLong);
		CHECK(store.Annotate(1, 1, 3, "  ").status == Status::EmptyText);
		CHECK(Read(store, 1, 1) == "one | first\x1F\n| on a blank line\x1F\nthree");

		// A note mark typed by Neuro is dropped.
		CHECK(store.Annotate(1, 1, 3, "x\x1Fy").status == Status::Ok);
		CHECK(Read(store, 1, 1).find("three | xy\x1F") != std::string::npos);

		// Writing on the blank line keeps its note. Two lines of text need two
		// blank lines in a row: nothing written moves.
		CHECK(store.WriteLine(1, 1, 1, "taken").status == Status::LineTaken);
		NotebookStore::WriteOutcome outcome = store.WriteLine(1, 1, 2, "two\nmore");
		CHECK(outcome.status == Status::NotEnoughLines && outcome.blankLines == 1 &&
		      outcome.textLines == 2);
		outcome = store.WriteLine(1, 1, 2, "two");
		CHECK(outcome.status == Status::Ok && outcome.lastPage == 1);
		CHECK(Read(store, 1, 1) ==
		      "one | first\x1F\ntwo | on a blank line\x1F\nthree | xy\x1F");

		// A run of blank lines takes as many lines of text, notes staying put.
		CHECK(store.Append(1, 3, "top\n\n\n\nbottom").status == Status::Ok);
		CHECK(store.Annotate(1, 3, 3, "mid").status == Status::Ok);
		CHECK(store.WriteLine(1, 3, 2, "a\nb\nc\nd").status ==
		      Status::NotEnoughLines);
		CHECK(store.WriteLine(1, 3, 2, "a\nb\nc").status == Status::Ok);
		CHECK(Read(store, 1, 3) == "top\na\nb | mid\x1F\nc\nbottom");

		// No room on the page: refused, nothing changes.
		CHECK(store.Append(1, 2, std::string(50, 'a') + "\n\ny").status ==
		      Status::Ok);
		std::string const before = Read(store, 1, 2);
		outcome = store.WriteLine(1, 2, 2, "ten chars!");
		CHECK(outcome.status == Status::NoRoom && outcome.roomLeft == 7);
		CHECK(store.Annotate(1, 2, 3, "abcdefgh").status == Status::NoRoom);
		CHECK(store.WriteLine(1, 2, 30, "x").status == Status::NoRoom);
		CHECK(Read(store, 1, 2) == before);

		// A line past the end is blank: the lines up to it stay blank, and a
		// blank page takes any line.
		CHECK(store.WriteLine(1, 3, 7, "far\nend").status == Status::Ok);
		CHECK(Read(store, 1, 3) == "top\na\nb | mid\x1F\nc\nbottom\n\nfar\nend");
		CHECK(store.WriteLine(1, 3, 5, "x\ny").status == Status::LineTaken);
		CHECK(store.WriteLine(1, 3, 6, "x\ny").status == Status::NotEnoughLines);
		CHECK(store.WriteLine(1, 3, 100000, "x").status == Status::NoRoom);
		NotebookStore fresh(root + L"\\fresh", 3, 60);
		outcome = fresh.WriteLine(1, 1, 2, "second");
		CHECK(outcome.status == Status::Ok && outcome.firstLine == 2);
		CHECK(Read(fresh, 1, 1) == "\nsecond");
		CHECK(fresh.WrittenPageCount(1) == 1);
		CHECK(fresh.Append(1, 3, "last").status == Status::Ok);
		CHECK(fresh.WrittenPageCount(1) == 2 && fresh.LastWrittenPage(1) == 3);
	}
	RemoveTree(root);
}

void AppendLineTests() {
	std::wstring const root = MakeTempRoot();
	{
		NotebookStore store(root, 3, 60);

		// The outcome names the lines, so a margin note can follow.
		NotebookStore::WriteOutcome outcome = store.Append(1, 1, "first");
		CHECK(outcome.firstLine == 1 && outcome.lastLine == 1);
		outcome = store.Append(1, 1, "second\nthird");
		CHECK(outcome.firstPage == 1 && outcome.firstLine == 2 &&
		      outcome.lastLine == 3);

		// Blank lines at the end of a note stay, to be written on later.
		outcome = store.Append(1, 1, "later:\n\n");
		CHECK(outcome.status == Status::Ok && outcome.firstLine == 4 &&
		      outcome.lastLine == 6);
		CHECK(Read(store, 1, 1) == "first\nsecond\nthird\nlater:\n\n");
		CHECK(store.WriteLine(1, 1, 5, "one\ntwo").status == Status::Ok);
		CHECK(Read(store, 1, 1) == "first\nsecond\nthird\nlater:\none\ntwo");
		CHECK(store.Append(1, 1, "\n  \n").status == Status::EmptyText);

		// A line past the end says how many there are.
		outcome = store.Annotate(1, 1, 9, "x");
		CHECK(outcome.status == Status::BadLine && outcome.lineCount == 6);
		outcome = store.Annotate(1, 2, 1, "x");
		CHECK(outcome.status == Status::BadLine && outcome.lineCount == 0);

		// Across pages: where it starts on the first, where it ends on the last.
		outcome = store.Append(1, 2, std::string(50, 'a'));
		outcome = store.Append(1, 2, "bbbbbbbbbb cccccccccc");
		CHECK(outcome.status == Status::Ok && outcome.firstPage == 3 &&
		      outcome.firstLine == 1 && outcome.lastPage == 3 &&
		      outcome.lastLine == 1);
	}
	RemoveTree(root);
}

void StrikeTests() {
	std::wstring const root = MakeTempRoot();
	{
		NotebookStore store(root, 3, 60);
		CHECK(store.Append(1, 1, "a cat and a cat\nthe dog").status == Status::Ok);

		// Twice on the page: needs an occurrence.
		NotebookStore::WriteOutcome outcome = store.Strike(1, 1, "cat", 0);
		CHECK(outcome.status == Status::Ambiguous && outcome.matches == 2);
		outcome = store.Strike(1, 1, "cat", 3);
		CHECK(outcome.status == Status::NoSuchOccurrence && outcome.matches == 2);
		CHECK(store.Strike(1, 1, "cow", 0).status == Status::NotFound);

		// Several strikes on one line, side by side.
		CHECK(store.Strike(1, 1, "cat", 2).status == Status::Ok);
		CHECK(store.Strike(1, 1, "cat", 0).status == Status::Ok);
		CHECK(Read(store, 1, 1) == "a ~~cat~~ and a ~~cat~~\nthe dog");
		CHECK(store.Strike(1, 1, "and", 0).status == Status::Ok);
		CHECK(Read(store, 1, 1) == "a ~~cat~~ ~~and~~ a ~~cat~~\nthe dog");

		// Text already struck is not struck again, even in part.
		CHECK(store.Strike(1, 1, "cat", 0).status == Status::AlreadyStruck);
		CHECK(store.Strike(1, 1, "at", 0).status == Status::AlreadyStruck);

		// Quoting the marks of struck text strikes nothing; the marks cost no
		// room.
		CHECK(store.Strike(1, 1, "a ~~cat~~\nthe", 0).status ==
		      Status::AlreadyStruck);
		outcome = store.Strike(1, 1, "the dog", 0);
		CHECK(outcome.status == Status::Ok);
		CHECK(outcome.roomLeft ==
		      60 - NotebookStore::CountWriting(L"a cat and a cat\nthe dog"));

		// Across the bar into a margin note: struck on each side, never the bar.
		CHECK(store.Append(1, 2, "left side\nnext").status == Status::Ok);
		CHECK(store.Annotate(1, 2, 1, "right").status == Status::Ok);
		CHECK(store.Annotate(1, 2, 2, "far").status == Status::Ok);
		CHECK(store.Strike(1, 2, " | ", 0).status == Status::NotFound);
		CHECK(store.Strike(1, 2, "side | right", 0).status == Status::Ok);
		CHECK(Read(store, 1, 2) == "left ~~side~~ | ~~right~~\x1F\nnext | far\x1F");
		CHECK(store.Strike(1, 2, " | far", 0).status == Status::Ok);
		CHECK(Read(store, 1, 2) ==
		      "left ~~side~~ | ~~right~~\x1F\nnext | ~~far~~\x1F");

		// Across line breaks: struck line by line, blank lines left alone.
		CHECK(store.Append(1, 3, "end of\nnext line\n\nlast").status == Status::Ok);
		CHECK(store.Strike(1, 3, "of\nnext", 0).status == Status::Ok);
		CHECK(Read(store, 1, 3) == "end ~~of~~\n~~next~~ line\n\nlast");
		CHECK(store.Strike(1, 3, "line\n\nlast", 0).status == Status::Ok);
		CHECK(Read(store, 1, 3) == "end ~~of~~\n~~next~~ ~~line~~\n\n~~last~~");
	}
	RemoveTree(root);
}

void TimeTests() {
	std::wstring const root = MakeTempRoot();
	{
		NotebookStore store(root, 3, 60);
		CHECK(!store.Times(1, 1).written);
		CHECK(store.Append(1, 1, "first").status == Status::Ok);

		// Pretend the page was started long ago; later writes keep that date.
		std::wstring const path = root + L"\\book1\\page_001.txt";
		HANDLE const file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, 0,
		                                nullptr, OPEN_EXISTING, 0, nullptr);
		SYSTEMTIME old = {2020, 1, 0, 2, 12, 0, 0, 0};
		FILETIME created = {};
		SystemTimeToFileTime(&old, &created);
		SetFileTime(file, &created, nullptr, nullptr);
		CloseHandle(file);

		CHECK(store.Append(1, 1, "second").status == Status::Ok);
		NotebookStore::PageTimes const times = store.Times(1, 1);
		CHECK(times.written);
		CHECK(times.created.substr(0, 4) == "2020");
		CHECK(times.modified.substr(0, 4) != "2020");
	}
	RemoveTree(root);
}

}  // namespace

void RunNotebookStoreTests() {
	SplitTests();
	StoreTests();
	CountAndFormatTests();
	LineTests();
	AppendLineTests();
	StrikeTests();
	TimeTests();
}
