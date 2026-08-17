#pragma once

#include <cstddef>
#include <string>

bool Utf16ToUtf8(wchar_t const *input, size_t length, std::string &output);
bool Utf8ToUtf16(char const *input, size_t length, std::wstring &output);
bool Utf16ToUtf8(wchar_t const *input, std::string &output);
bool Utf8ToUtf16(char const *input, std::wstring &output);
