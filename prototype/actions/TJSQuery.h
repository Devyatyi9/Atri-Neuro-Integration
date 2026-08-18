#pragma once

#include <string>

// TJS fact-query helpers for the "query facts in TJS, decide in C++" pattern.
// Each helper runs one fixed TJS probe through ExecuteTJSSync and converts its
// single result value. See docs-local/architecture/tjs-cpp-boundary.md.

bool QueryString(wchar_t const *expr, std::string &out, char const *tag);
bool QueryBool(wchar_t const *expr, bool &out, char const *tag);