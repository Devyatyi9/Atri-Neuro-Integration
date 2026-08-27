#include "ActionJson.h"

#include <cctype>
#include <cstring>

#include "ParseNumber.h"

namespace {

// Position of the value that follows `key`, or npos.
size_t FindValueStart(std::string const &json, char const *key) {
	size_t const keyPos = json.find(key);
	if (keyPos == std::string::npos) {
		return std::string::npos;
	}
	size_t const colon = json.find(':', keyPos + strlen(key));
	if (colon == std::string::npos) {
		return std::string::npos;
	}
	return json.find_first_not_of(" \t\r\n", colon + 1);
}

}  // namespace

bool IsEmptyJsonObject(std::string const &data) {
	size_t const first = data.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return true;
	}
	if (data[first] != '{') {
		return false;
	}
	size_t const last = data.find_last_not_of(" \t\r\n");
	return last != std::string::npos && last == first + 1 && data[last] == '}';
}

// A JSON number token is only accepted here when it is a whole integer that
// fits, and when the value really ends where the integer ends. The old scan
// took any run of digits and minus signs and handed it to atoi, so "2.9"
// arrived as slot 2, "1-2" as page 1, and an overflowing literal as whatever
// atoi happened to produce -- each of them a number in range, accepted by the
// caller, and pointing at the wrong thing. A malformed value has to be
// refused, not rounded.
bool ParseJsonInteger(std::string const &json, char const *key, int &out) {
	size_t const start = FindValueStart(json, key);
	if (start == std::string::npos) {
		return false;
	}
	size_t end = start;
	if (end < json.size() && json[end] == '-') {
		++end;
	}
	size_t const digits = end;
	while (end < json.size() && isdigit(static_cast<unsigned char>(json[end]))) {
		++end;
	}
	if (end == digits) {
		return false;
	}
	// Anything but a delimiter here means the value was not an integer:
	// a fraction, an exponent, a second sign, or a quoted string.
	if (end < json.size() && strchr(",}] \t\r\n", json[end]) == nullptr) {
		return false;
	}

	return ParseIntToken(json.substr(start, end - start), out);
}

bool ParseJsonBoolean(std::string const &json, char const *key, int &out) {
	size_t const start = FindValueStart(json, key);
	if (start == std::string::npos) {
		return false;
	}
	// The literal has to end where it ends: "truthy" is not true.
	auto const literal = [&json, start](char const *text, size_t size) {
		if (json.compare(start, size, text) != 0) {
			return false;
		}
		size_t const after = start + size;
		return after >= json.size() ||
		       strchr(",}] \t\r\n", json[after]) != nullptr;
	};
	if (literal("true", 4)) {
		out = 1;
		return true;
	}
	if (literal("false", 5)) {
		out = 0;
		return true;
	}
	return false;
}
