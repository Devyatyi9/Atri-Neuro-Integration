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

}  // namespace

void RunNotebookStoreTests() {
	SplitTests();
	StoreTests();
}
