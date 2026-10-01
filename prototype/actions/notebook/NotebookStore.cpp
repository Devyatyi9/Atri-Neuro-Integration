#include "NotebookStore.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <utility>

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

// Spaces and tabs only: blank lines at either end of a note are kept, so room
// can be left to fill in later.
std::wstring WithoutEdgeSpaces(std::wstring const &text) {
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && (text[begin] == L' ' || text[begin] == L'\t')) {
		++begin;
	}
	while (end > begin && (text[end - 1] == L' ' || text[end - 1] == L'\t')) {
		--end;
	}
	return text.substr(begin, end - begin);
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

// Ends a line that has a margin note, so a line gets one. Never shown and never
// accepted from Neuro's text.
constexpr wchar_t kNoteMark = L'\x1F';
wchar_t const *const kStrike = L"~~";

std::wstring WithoutNoteMarks(std::wstring const &text) {
	std::wstring out;
	out.reserve(text.size());
	for (wchar_t c : text) {
		if (c != kNoteMark) {
			out += c;
		}
	}
	return out;
}

std::vector<std::wstring> SplitLines(std::wstring const &text) {
	std::vector<std::wstring> lines;
	if (text.empty()) {
		return lines;
	}
	size_t start = 0;
	for (;;) {
		size_t const end = text.find(L'\n', start);
		lines.push_back(text.substr(start, end - start));
		if (end == std::wstring::npos) {
			break;
		}
		start = end + 1;
	}
	return lines;
}

std::wstring JoinLines(std::vector<std::wstring> const &lines) {
	std::wstring text;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i > 0) {
			text += L'\n';
		}
		text += lines[i];
	}
	return text;
}

// A line on disk is "writing | note<mark>", "| note<mark>" when the line has
// only the note, or plain writing.
struct LineParts {
	std::wstring writing;
	std::wstring note;
	bool hasNote = false;
};

LineParts ParseLine(std::wstring const &line) {
	LineParts parts;
	size_t const bar = line.rfind(L"| ");
	if (line.empty() || line.back() != kNoteMark || bar == std::wstring::npos) {
		parts.writing = line;
		return parts;
	}
	parts.hasNote = true;
	parts.note = line.substr(bar + 2, line.size() - bar - 3);
	parts.writing = line.substr(0, bar);
	while (!parts.writing.empty() && parts.writing.back() == L' ') {
		parts.writing.pop_back();
	}
	return parts;
}

std::wstring ComposeLine(LineParts const &parts) {
	if (!parts.hasNote) {
		return parts.writing;
	}
	std::wstring const margin = L"| " + parts.note + kNoteMark;
	return parts.writing.empty() ? margin : parts.writing + L" " + margin;
}

// Where struck text already is: from each opening "~~" to just past its closing
// one. An unclosed "~~" strikes nothing.
std::vector<std::pair<size_t, size_t>> StruckSpans(std::wstring const &text) {
	std::vector<std::pair<size_t, size_t>> spans;
	size_t from = 0;
	for (;;) {
		size_t const open = text.find(kStrike, from);
		if (open == std::wstring::npos) {
			break;
		}
		size_t const close = text.find(kStrike, open + 2);
		if (close == std::wstring::npos) {
			break;
		}
		spans.push_back({open, close + 2});
		from = close + 2;
	}
	return spans;
}

std::string FormatLocalTime(FILETIME const &utc) {
	SYSTEMTIME universal = {};
	SYSTEMTIME local = {};
	if (!FileTimeToSystemTime(&utc, &universal) ||
	    !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local)) {
		return "";
	}
	char text[32] = {};
	snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u", local.wYear,
	         local.wMonth, local.wDay, local.wHour, local.wMinute);
	return text;
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
	// The replacement would carry the temporary file's creation time, and the
	// page's is when it was first written on.
	WIN32_FILE_ATTRIBUTE_DATA old = {};
	bool const hadPage =
	    GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &old) != 0;
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
	if (complete && hadPage) {
		// Losing the date is no reason to lose the writing.
		SetFileTime(file, &old.ftCreationTime, nullptr, nullptr);
	}
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

int NotebookStore::CountWriting(std::wstring const &text) {
	int marks = 0;
	for (auto const &span : StruckSpans(text)) {
		(void)span;
		marks += 4;
	}
	return CountCharacters(WithoutNoteMarks(text)) - marks;
}

std::wstring NotebookStore::NumberedLines(std::wstring const &text) {
	std::vector<std::wstring> const lines = SplitLines(text);
	int const digits = (int)std::to_wstring(lines.size()).size();
	std::wstring out;
	for (size_t i = 0; i < lines.size(); ++i) {
		LineParts const parts = ParseLine(lines[i]);
		std::wstring number = std::to_wstring(i + 1);
		number.insert(0, (size_t)digits - number.size(), L' ');
		std::wstring line = number + L" | " + parts.writing;
		if (parts.hasNote) {
			int const used = CountCharacters(parts.writing);
			if (used < kMarginColumn) {
				line.append(kMarginColumn - used, L' ');
			}
			line += L" | " + parts.note;
		}
		while (!line.empty() && line.back() == L' ') {
			line.pop_back();
		}
		if (i > 0) {
			out += L'\n';
		}
		out += line;
	}
	return out;
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

std::vector<int> NotebookStore::WrittenPages(int book) {
	std::vector<int> pages;
	if (!ValidPage(book, 1)) {
		return pages;
	}
	std::lock_guard<std::mutex> lock(m_Mutex);
	for (int page = 1; page <= m_PageCount; ++page) {
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (GetFileAttributesExW(PagePath(book, page).c_str(),
		                         GetFileExInfoStandard, &data) &&
		    (data.nFileSizeLow != 0 || data.nFileSizeHigh != 0)) {
			pages.push_back(page);
		}
	}
	return pages;
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
	std::wstring const note =
	    WithoutEdgeSpaces(WithoutNoteMarks(WithoutCarriageReturns(wide)));
	if (Trimmed(note).empty()) {
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
	int const room = m_PageCapacity - CountWriting(existing) - separator;
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
		int const lines = (int)SplitLines(text).size();
		if (outcome.firstPage == 0) {
			outcome.firstPage = target;
			outcome.firstLine = lines - (int)SplitLines(pieces[i]).size() + 1;
		}
		outcome.lastPage = target;
		outcome.lastLine = lines;
		outcome.roomLeft = m_PageCapacity - CountWriting(text);
	}
	outcome.status = Status::Ok;
	return outcome;
}

NotebookStore::Status NotebookStore::SavePageLocked(int book,
                                                    int page,
                                                    std::wstring const &text) {
	std::string bytes;
	if (!EnsureFolder(BookFolder(book)) ||
	    !Utf16ToUtf8(text.c_str(), text.size(), bytes) ||
	    !ReplaceFileContents(PagePath(book, page), bytes)) {
		return Status::IoError;
	}
	return Status::Ok;
}

NotebookStore::WriteOutcome NotebookStore::WriteLine(int book,
                                                     int page,
                                                     int line,
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
	std::wstring const text =
	    Trimmed(WithoutNoteMarks(WithoutCarriageReturns(wide)));
	if (text.empty()) {
		outcome.status = Status::EmptyText;
		return outcome;
	}

	std::lock_guard<std::mutex> lock(m_Mutex);
	std::wstring existing;
	outcome.status = ReadPageLocked(book, page, existing);
	if (outcome.status != Status::Ok) {
		return outcome;
	}
	std::vector<std::wstring> lines = SplitLines(existing);
	// The capacity check below would refuse it anyway, with a huge allocation
	// first.
	if (line < 1 || line > m_PageCapacity) {
		outcome.status = Status::NoRoom;
		outcome.roomLeft = m_PageCapacity - CountWriting(existing);
		return outcome;
	}
	// Each line of the text takes one blank line; margin notes stay where
	// they are.
	std::vector<std::wstring> const added = SplitLines(text);
	// Past the end the page is blank.
	if (lines.size() < line - 1 + added.size()) {
		lines.resize(line - 1 + added.size());
	}
	auto blank = [&](size_t index) {
		return Trimmed(ParseLine(lines[index]).writing).empty();
	};
	if (!blank(line - 1)) {
		outcome.status = Status::LineTaken;
		return outcome;
	}
	size_t free = 0;
	while (line - 1 + free < lines.size() && free < added.size() &&
	       blank(line - 1 + free)) {
		++free;
	}
	if (free < added.size()) {
		while (line - 1 + free < lines.size() && blank(line - 1 + free)) {
			++free;
		}
		outcome.status = Status::NotEnoughLines;
		outcome.blankLines = (int)free;
		outcome.textLines = (int)added.size();
		return outcome;
	}
	for (size_t i = 0; i < added.size(); ++i) {
		LineParts parts = ParseLine(lines[line - 1 + i]);
		parts.writing = added[i];
		lines[line - 1 + i] = ComposeLine(parts);
	}
	std::wstring const updated = JoinLines(lines);

	if (CountWriting(updated) > m_PageCapacity) {
		outcome.status = Status::NoRoom;
		outcome.roomLeft = m_PageCapacity - CountWriting(existing);
		return outcome;
	}
	outcome.status = SavePageLocked(book, page, updated);
	if (outcome.status == Status::Ok) {
		outcome.firstPage = outcome.lastPage = page;
		outcome.firstLine = line;
		outcome.lastLine = line + (int)added.size() - 1;
		outcome.roomLeft = m_PageCapacity - CountWriting(updated);
	}
	return outcome;
}

NotebookStore::WriteOutcome NotebookStore::Annotate(int book,
                                                    int page,
                                                    int line,
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
	// A note in the margin is one line long.
	std::wstring note = WithoutNoteMarks(WithoutCarriageReturns(wide));
	for (wchar_t &c : note) {
		if (c == L'\n' || c == L'\t') {
			c = L' ';
		}
	}
	note = Trimmed(note);
	if (note.empty()) {
		outcome.status = Status::EmptyText;
		return outcome;
	}
	if (CountCharacters(note) > kMarginNoteMax) {
		outcome.status = Status::TooLong;
		return outcome;
	}

	std::lock_guard<std::mutex> lock(m_Mutex);
	std::wstring existing;
	outcome.status = ReadPageLocked(book, page, existing);
	if (outcome.status != Status::Ok) {
		return outcome;
	}
	std::vector<std::wstring> lines = SplitLines(existing);
	if (line < 1 || line > (int)lines.size()) {
		outcome.status = Status::BadLine;
		outcome.lineCount = (int)lines.size();
		return outcome;
	}
	LineParts parts = ParseLine(lines[line - 1]);
	if (parts.hasNote) {
		outcome.status = Status::HasNote;
		return outcome;
	}
	parts.hasNote = true;
	parts.note = note;
	lines[line - 1] = ComposeLine(parts);
	std::wstring const updated = JoinLines(lines);

	if (CountWriting(updated) > m_PageCapacity) {
		outcome.status = Status::NoRoom;
		outcome.roomLeft = m_PageCapacity - CountWriting(existing);
		return outcome;
	}
	outcome.status = SavePageLocked(book, page, updated);
	if (outcome.status == Status::Ok) {
		outcome.firstPage = outcome.lastPage = page;
		outcome.firstLine = outcome.lastLine = line;
		outcome.roomLeft = m_PageCapacity - CountWriting(updated);
	}
	return outcome;
}

NotebookStore::WriteOutcome NotebookStore::Strike(int book,
                                                  int page,
                                                  std::string const &quote,
                                                  int occurrence) {
	WriteOutcome outcome;
	if (!ValidPage(book, page)) {
		outcome.status = Status::BadPage;
		return outcome;
	}
	std::wstring wide;
	if (!Utf8ToUtf16(quote.data(), quote.size(), wide)) {
		outcome.status = Status::BadText;
		return outcome;
	}
	std::wstring const wanted =
	    Trimmed(WithoutNoteMarks(WithoutCarriageReturns(wide)));
	if (wanted.empty()) {
		outcome.status = Status::EmptyText;
		return outcome;
	}

	std::lock_guard<std::mutex> lock(m_Mutex);
	std::wstring existing;
	outcome.status = ReadPageLocked(book, page, existing);
	if (outcome.status != Status::Ok) {
		return outcome;
	}
	// Only text still standing counts: not inside what is already struck.
	std::vector<std::pair<size_t, size_t>> const struck = StruckSpans(existing);
	// The " | " between a line and its margin note is not writing: a quote
	// running across it is struck on each side, never across the bar.
	std::vector<std::pair<size_t, size_t>> separators;
	// Nor is a line break: each line is struck on its own.
	size_t offset = 0;
	for (std::wstring const &text : SplitLines(existing)) {
		LineParts const parts = ParseLine(text);
		if (parts.hasNote) {
			size_t const bar = offset + text.rfind(L"| ");
			separators.push_back({parts.writing.empty() ? bar : bar - 1, bar + 2});
		}
		offset += text.size() + 1;
		if (offset <= existing.size()) {
			separators.push_back({offset - 1, offset});
		}
	}
	std::vector<size_t> found;
	bool inStruck = false;
	for (size_t at = existing.find(wanted); at != std::wstring::npos;
	     at = existing.find(wanted, at + 1)) {
		size_t const end = at + wanted.size();
		bool overlaps = false;
		for (auto const &span : struck) {
			if (at < span.second && end > span.first) {
				overlaps = true;
				inStruck = true;
				break;
			}
		}
		for (auto const &separator : separators) {
			// Nothing but the separator itself.
			if (at >= separator.first && end <= separator.second) {
				overlaps = true;
				break;
			}
		}
		if (!overlaps) {
			found.push_back(at);
		}
	}
	outcome.matches = (int)found.size();
	if (found.empty()) {
		outcome.status = inStruck ? Status::AlreadyStruck : Status::NotFound;
		return outcome;
	}
	if (occurrence > (int)found.size() || occurrence < 0) {
		outcome.status = Status::NoSuchOccurrence;
		return outcome;
	}
	if (occurrence == 0 && found.size() > 1) {
		outcome.status = Status::Ambiguous;
		return outcome;
	}
	size_t const at = found[occurrence == 0 ? 0 : occurrence - 1];
	// The match, less any separator inside it, in pieces from the start.
	std::vector<std::pair<size_t, size_t>> pieces = {{at, at + wanted.size()}};
	for (auto const &separator : separators) {
		std::vector<std::pair<size_t, size_t>> cut;
		for (auto const &piece : pieces) {
			if (piece.second <= separator.first || piece.first >= separator.second) {
				cut.push_back(piece);
				continue;
			}
			if (piece.first < separator.first) {
				cut.push_back({piece.first, separator.first});
			}
			if (piece.second > separator.second) {
				cut.push_back({separator.second, piece.second});
			}
		}
		pieces = cut;
	}
	std::wstring updated = existing;
	// From the last piece back, so earlier positions stay valid.
	for (auto piece = pieces.rbegin(); piece != pieces.rend(); ++piece) {
		updated.insert(piece->second, kStrike);
		updated.insert(piece->first, kStrike);
	}

	outcome.status = SavePageLocked(book, page, updated);
	if (outcome.status == Status::Ok) {
		outcome.firstPage = outcome.lastPage = page;
		outcome.roomLeft = m_PageCapacity - CountWriting(updated);
	}
	return outcome;
}

NotebookStore::PageTimes NotebookStore::Times(int book, int page) {
	PageTimes times;
	if (!ValidPage(book, page)) {
		return times;
	}
	std::lock_guard<std::mutex> lock(m_Mutex);
	WIN32_FILE_ATTRIBUTE_DATA data = {};
	if (!GetFileAttributesExW(PagePath(book, page).c_str(), GetFileExInfoStandard,
	                          &data) ||
	    (data.nFileSizeLow == 0 && data.nFileSizeHigh == 0)) {
		return times;
	}
	times.written = true;
	times.created = FormatLocalTime(data.ftCreationTime);
	times.modified = FormatLocalTime(data.ftLastWriteTime);
	return times;
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
