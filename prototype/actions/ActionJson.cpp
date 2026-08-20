#include "ActionJson.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

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

bool ParseJsonInteger(std::string const &json, char const *key, int &out) {
	size_t const start = FindValueStart(json, key);
	if (start == std::string::npos) {
		return false;
	}
	size_t end = start;
	while (end < json.size() &&
	       (isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) {
		++end;
	}
	if (end == start) {
		return false;
	}
	out = atoi(json.substr(start, end - start).c_str());
	return true;
}

bool ParseJsonBoolean(std::string const &json, char const *key, int &out) {
	size_t const start = FindValueStart(json, key);
	if (start == std::string::npos) {
		return false;
	}
	if (json.compare(start, 4, "true") == 0) {
		out = 1;
		return true;
	}
	if (json.compare(start, 5, "false") == 0) {
		out = 0;
		return true;
	}
	return false;
}
