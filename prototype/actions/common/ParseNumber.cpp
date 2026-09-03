#include "ParseNumber.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

bool ParseIntToken(std::string const &token, int &out) {
	if (token.empty()) {
		return false;
	}
	// strtol accepts a leading blank or a plus sign of its own accord. Neither
	// belongs in a token the caller claims to have isolated, and JSON has no
	// plus sign at all, so both are turned away before it is asked.
	if (token[0] == ' ' || token[0] == '\t' || token[0] == '+') {
		return false;
	}
	char const *const start = token.c_str();
	char *end = nullptr;
	errno = 0;
	long const value = strtol(start, &end, 10);
	// strtol also stops at the first character it cannot use and calls that a
	// success, so the end has to land exactly on the end of the token.
	if (end != start + token.size() || errno == ERANGE || value < INT_MIN ||
	    value > INT_MAX) {
		return false;
	}
	out = static_cast<int>(value);
	return true;
}

bool ParseWholeInt(std::string const &text, int &out) {
	size_t const first = text.find_first_not_of(" \t");
	if (first == std::string::npos) {
		return false;
	}
	size_t const last = text.find_last_not_of(" \t");
	return ParseIntToken(text.substr(first, last - first + 1), out);
}
