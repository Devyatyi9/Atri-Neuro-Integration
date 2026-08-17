#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <climits>
#include <cstring>
#include <cwchar>

#include "UtfConversion.h"

bool Utf16ToUtf8(wchar_t const *input, size_t length, std::string &output) {
	output.clear();
	if (!input)
		return length == 0;
	if (length > INT_MAX)
		return false;
	if (length == 0)
		return true;

	int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input,
	                                   static_cast<int>(length), nullptr, 0,
	                                   nullptr, nullptr);
	if (required <= 0)
		return false;

	output.resize(required);
	int converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input,
	                                    static_cast<int>(length), &output[0],
	                                    required, nullptr, nullptr);
	if (converted != required) {
		output.clear();
		return false;
	}
	return true;
}

bool Utf8ToUtf16(char const *input, size_t length, std::wstring &output) {
	output.clear();
	if (!input)
		return length == 0;
	if (length > INT_MAX)
		return false;
	if (length == 0)
		return true;

	int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input,
	                                   static_cast<int>(length), nullptr, 0);
	if (required <= 0)
		return false;

	output.resize(required);
	int converted =
	    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input,
	                        static_cast<int>(length), &output[0], required);
	if (converted != required) {
		output.clear();
		return false;
	}
	return true;
}

bool Utf16ToUtf8(wchar_t const *input, std::string &output) {
	output.clear();
	if (!input)
		return false;
	return Utf16ToUtf8(input, std::wcslen(input), output);
}

bool Utf8ToUtf16(char const *input, std::wstring &output) {
	output.clear();
	if (!input)
		return false;
	return Utf8ToUtf16(input, std::strlen(input), output);
}
