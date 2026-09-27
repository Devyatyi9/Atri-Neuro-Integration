#include "NotebookStore.h"

#include <windows.h>

#include <cwchar>

#include "UtfConversion.h"

namespace {

constexpr int kMaxBook = 9;
constexpr int kReplaceAttempts = 5;
constexpr DWORD kReplaceRetryMs = 50;

bool IsHighSurrogate(wchar_t c) {
	return c >= 0xD800 && c <= 0xDBFF;
}

bool IsSpace(wchar_t c) {
	return c == L' ' || c == L'\n' || c == L'\t';
}

// Index just past `count` characters from `from`, never inside a surrogate
// pair; the end of the text when it runs out first.
size_t IndexAfter(std::wstring const &text, size_t from, int count) {
	size_t index = from;
	for (int taken = 0; taken < count && index < text.size(); ++taken) {
		index += (IsHighSurrogate(text[index]) && index + 1 < text.size()) ? 2 : 1;
	}
	return index;
}

std::wstring WithoutCarriageReturns(std::wstring const &text) {
	std::wstring out;
	out.reserve(text.size());
	for (wchar_t c : text) {
		if (c != L'\r') {
			out += c;
		}
	}
	return out;
}

std::wstring Trimmed(std::wstring const &text) {
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && IsSpace(text[begin])) {
		++begin;
	}
	while (end > begin && IsSpace(text[end - 1])) {
		--end;
	}
	return text.substr(begin, end - begin);
}

// Creates every missing folder along the path.
bool EnsureFolder(std::wstring const &path) {
	for (size_t i = 0; i <= path.size(); ++i) {
		if (i != path.size() && path[i] != L'\\' && path[i] != L'/') {
			continue;
		}
		std::wstring const prefix = path.substr(0, i);
		if (prefix.empty() || prefix.back() == L':') {
			continue;
		}
		if (!CreateDirectoryW(prefix.c_str(), nullptr) &&
		    GetLastError() != ERROR_ALREADY_EXISTS) {
			return false;
		}
	}
	return true;
}

// Written beside the page and moved over it, so a write cut short leaves the
// old page rather than an empty one.
bool ReplaceFileContents(std::wstring const &path, std::string const &bytes) {
	std::wstring const temporary = path + L".tmp";
	HANDLE const file =
	    CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
	                FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD written = 0;
	bool const complete =
	    (bytes.empty() ||
	     WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr)) &&
	    written == bytes.size() && FlushFileBuffers(file);
	CloseHandle(file);
	if (!complete) {
		DeleteFileW(temporary.c_str());
		return false;
	}
	// A scanner or indexer can hold the old page for a moment.
	for (int attempt = 0; attempt < kReplaceAttempts; ++attempt) {
		if (MoveFileExW(temporary.c_str(), path.c_str(),
		                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			return true;
		}
		Sleep(kReplaceRetryMs);
	}
	DeleteFileW(temporary.c_str());
	return false;
}

}  // namespace

NotebookStore::NotebookStore(std::wstring root, int pageCount, int pageCapacity)
    : m_Root(std::move(root)),
      m_PageCount(pageCount),
      m_PageCapacity(pageCapacity) { }

int NotebookStore::CountCharacters(std::wstring const &text) {
	int count = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		if (IsHighSurrogate(text[i]) && i + 1 < text.size()) {
			++i;
		}
		++count;
	}
	return count;
}

std::vector<std::wstring> NotebookStore::SplitForPages(std::wstring const &text,
                                                       int room,
                                                       int capacity) {
	std::vector<std::wstring> pieces;
	size_t position = 0;
	int limit = room > 0 ? room : 0;
	bool first = true;
	while (position < text.size()) {
		size_t const hardCut = IndexAfter(text, position, limit);
		if (hardCut >= text.size()) {
			pieces.push_back(text.substr(position));
			break;
		}
		size_t cut = hardCut;
		while (cut > position && !IsSpace(text[cut])) {
			--cut;
		}
		if (cut == position) {
			if (first && limit < capacity) {
				// Not even one word fits after the existing writing.
				pieces.push_back(std::wstring());
				first = false;
				limit = capacity;
				continue;
			}
			// One word longer than a page: split it where the page ends.
			cut = hardCut;
		}
		pieces.push_back(Trimmed(text.substr(position, cut - position)));
		position = cut;
		while (position < text.size() && IsSpace(text[position])) {
			++position;
		}
		first = false;
		limit = capacity;
	}
	return pieces;
}

std::wstring NotebookStore::BookFolder(int book) const {
	return m_Root + L"\\book" + std::to_wstring(book);
}

std::wstring NotebookStore::PagePath(int book, int page) const {
	wchar_t name[32] = {};
	swprintf(name, 32, L"\\page_%03d.txt", page);
	return BookFolder(book) + name;
}

bool NotebookStore::ValidPage(int book, int page) const {
	return book >= 1 && book <= kMaxBook && page >= 1 && page <= m_PageCount;
}

NotebookStore::Status NotebookStore::ReadPageLocked(int book,
                                                    int page,
                                                    std::wstring &text) {
	text.clear();
	HANDLE const file =
	    CreateFileW(PagePath(book, page).c_str(), GENERIC_READ,
	                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		DWORD const error = GetLastError();
		return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
		           ? Status::Ok
		           : Status::IoError;
	}
	std::string bytes;
	LARGE_INTEGER size = {};
	bool ok = GetFileSizeEx(file, &size) != 0 && size.QuadPart < (1 << 20);
	if (ok && size.QuadPart > 0) {
		bytes.resize((size_t)size.QuadPart);
		DWORD read = 0;
		ok = ReadFile(file, &bytes[0], (DWORD)bytes.size(), &read, nullptr) &&
		     read == bytes.size();
	}
	CloseHandle(file);
	if (!ok) {
		return Status::IoError;
	}
	// A page opened and saved in an editor may come back with a BOM and CRLF.
	size_t skip = 0;
	if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
	    (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) {
		skip = 3;
	}
	std::wstring wide;
	if (!Utf8ToUtf16(bytes.data() + skip, bytes.size() - skip, wide)) {
		return Status::BadText;
	}
	text = WithoutCarriageReturns(wide);
	return Status::Ok;
}

NotebookStore::Status NotebookStore::ReadPage(int book,
                                              int page,
                                              std::string &utf8) {
	utf8.clear();
	if (!ValidPage(book, page)) {
		return Status::BadPage;
	}
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::wstring text;
	Status const status = ReadPageLocked(book, page, text);
	if (status != Status::Ok) {
		return status;
	}
	return Utf16ToUtf8(text.c_str(), text.size(), utf8) ? Status::Ok
	                                                    : Status::BadText;
}

int NotebookStore::LastWrittenPage(int book) {
	if (!ValidPage(book, 1)) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(m_Mutex);
	for (int page = m_PageCount; page >= 1; --page) {
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (GetFileAttributesExW(PagePath(book, page).c_str(),
		                         GetFileExInfoStandard, &data) &&
		    (data.nFileSizeLow != 0 || data.nFileSizeHigh != 0)) {
			return page;
		}
	}
	return 0;
}

NotebookStore::WriteOutcome NotebookStore::Append(int book,
                                                  int page,
                                                  std::string const &utf8) {
	WriteOutcome outcome;
	if (!ValidPage(book, page)) {
		outcome.status = Status::BadPage;
		return outcome;
	}
	std::wstring wide;
	if (!Utf8ToUtf16(utf8.data(), utf8.size(), wide)) {
		outcome.status = Status::BadText;
		return outcome;
	}
	std::wstring const note = Trimmed(WithoutCarriageReturns(wide));
	if (note.empty()) {
		outcome.status = Status::EmptyText;
		return outcome;
	}

	std::lock_guard<std::mutex> lock(m_Mutex);
	std::wstring existing;
	Status const readStatus = ReadPageLocked(book, page, existing);
	if (readStatus != Status::Ok) {
		outcome.status = readStatus;
		return outcome;
	}
	// A new note starts on its own line.
	int const separator = existing.empty() ? 0 : 1;
	int const room = m_PageCapacity - CountCharacters(existing) - separator;
	std::vector<std::wstring> const pieces =
	    SplitForPages(note, room, m_PageCapacity);

	// Every page after the first must exist and be blank before anything is
	// written, so a refused note leaves the notebook exactly as it was.
	int freeRoom = room > 0 ? room : 0;
	bool fits = page + (int)pieces.size() - 1 <= m_PageCount;
	for (size_t i = 1; i < pieces.size() && fits; ++i) {
		std::wstring next;
		Status const status = ReadPageLocked(book, page + (int)i, next);
		if (status != Status::Ok) {
			outcome.status = status;
			return outcome;
		}
		fits = next.empty();
	}
	if (!fits) {
		for (int next = page + 1; next <= m_PageCount; ++next) {
			std::wstring text;
			if (ReadPageLocked(book, next, text) != Status::Ok || !text.empty()) {
				break;
			}
			freeRoom += m_PageCapacity;
		}
		outcome.status = Status::NoRoom;
		outcome.roomLeft = freeRoom;
		return outcome;
	}

	if (!EnsureFolder(BookFolder(book))) {
		outcome.status = Status::IoError;
		return outcome;
	}
	for (size_t i = 0; i < pieces.size(); ++i) {
		if (pieces[i].empty()) {
			continue;
		}
		int const target = page + (int)i;
		std::wstring text = pieces[i];
		if (i == 0 && !existing.empty()) {
			text = existing + L"\n" + text;
		}
		std::string bytes;
		if (!Utf16ToUtf8(text.c_str(), text.size(), bytes) ||
		    !ReplaceFileContents(PagePath(book, target), bytes)) {
			// Pages already written stay: the note is cut short, not lost,
			// and the outcome says where it stops.
			outcome.status = Status::IoError;
			return outcome;
		}
		if (outcome.firstPage == 0) {
			outcome.firstPage = target;
		}
		outcome.lastPage = target;
		outcome.roomLeft = m_PageCapacity - CountCharacters(text);
	}
	outcome.status = Status::Ok;
	return outcome;
}

void NotebookStore::RemoveLeftovers(int book) {
	if (!ValidPage(book, 1)) {
		return;
	}
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::wstring const folder = BookFolder(book);
	WIN32_FIND_DATAW found = {};
	HANDLE const search = FindFirstFileW((folder + L"\\*.tmp").c_str(), &found);
	if (search == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		DeleteFileW((folder + L"\\" + found.cFileName).c_str());
	} while (FindNextFileW(search, &found));
	FindClose(search);
}
