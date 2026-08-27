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

namespace {

// One \uXXXX, as a code unit. Refuses anything that is not four hex digits.
bool ReadHex4(std::string const &json, size_t at, unsigned &out) {
	if (at + 4 > json.size()) {
		return false;
	}
	unsigned value = 0;
	for (size_t index = at; index < at + 4; ++index) {
		char const digit = json[index];
		value <<= 4;
		if (digit >= '0' && digit <= '9') {
			value |= static_cast<unsigned>(digit - '0');
		} else if (digit >= 'a' && digit <= 'f') {
			value |= static_cast<unsigned>(digit - 'a' + 10);
		} else if (digit >= 'A' && digit <= 'F') {
			value |= static_cast<unsigned>(digit - 'A' + 10);
		} else {
			return false;
		}
	}
	out = value;
	return true;
}

void AppendUtf8(unsigned codePoint, std::string &out) {
	if (codePoint < 0x80) {
		out += static_cast<char>(codePoint);
	} else if (codePoint < 0x800) {
		out += static_cast<char>(0xc0 | (codePoint >> 6));
		out += static_cast<char>(0x80 | (codePoint & 0x3f));
	} else if (codePoint < 0x10000) {
		out += static_cast<char>(0xe0 | (codePoint >> 12));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
		out += static_cast<char>(0x80 | (codePoint & 0x3f));
	} else {
		out += static_cast<char>(0xf0 | (codePoint >> 18));
		out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
		out += static_cast<char>(0x80 | (codePoint & 0x3f));
	}
}

}  // namespace

bool ParseJsonString(std::string const &json, char const *key,
                     std::string &out) {
	size_t const start = FindValueStart(json, key);
	if (start == std::string::npos || json[start] != '"') {
		return false;
	}

	std::string text;
	size_t cursor = start + 1;
	while (cursor < json.size()) {
		char const character = json[cursor];
		if (character == '"') {
			out.swap(text);
			return true;
		}
		if (character != '\\') {
			// A raw control character is not legal inside a JSON string, and a
			// stray one usually means the payload was cut short.
			if (static_cast<unsigned char>(character) < 0x20) {
				return false;
			}
			text += character;
			++cursor;
			continue;
		}
		if (cursor + 1 >= json.size()) {
			return false;
		}
		char const escape = json[cursor + 1];
		cursor += 2;
		switch (escape) {
			case '"': text += '\"'; break;
			case '\\': text += '\\'; break;
			case '/': text += '/'; break;
			case 'b': text += '\b'; break;
			case 'f': text += '\f'; break;
			case 'n': text += '\n'; break;
			case 'r': text += '\r'; break;
			case 't': text += '\t'; break;
			case 'u': {
				unsigned unit = 0;
				if (!ReadHex4(json, cursor, unit)) {
					return false;
				}
				cursor += 4;
				if (unit >= 0xd800 && unit <= 0xdbff) {
					// A high surrogate has to be followed by its low half; on its
					// own it is not a character, and passing it through would put
					// broken text on the screen.
					unsigned low = 0;
					if (cursor + 1 >= json.size() || json[cursor] != '\\' ||
					    json[cursor + 1] != 'u' ||
					    !ReadHex4(json, cursor + 2, low) || low < 0xdc00 ||
					    low > 0xdfff) {
						return false;
					}
					cursor += 6;
					unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
				} else if (unit >= 0xdc00 && unit <= 0xdfff) {
					return false;
				}
				AppendUtf8(unit, text);
				break;
			}
			default:
				// Not an escape JSON defines. Refused rather than passed through
				// as the letter itself.
				return false;
		}
	}
	// Ran off the end without a closing quote.
	return false;
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
