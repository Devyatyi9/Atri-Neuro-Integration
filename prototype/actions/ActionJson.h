#pragma once

#include <string>

// Minimal readers for the action `data` payloads.
//
// These are deliberately hand-rolled rather than backed by a JSON library: the
// payloads are tiny fixed shapes defined by our own action schemas, and the
// project does not pull in a parser while it exchanges only SDK-owned JSON
// strings while the project exchanges only SDK-owned JSON. Revisit together
// with that decision, not separately.

// True when `data` is absent, blank, or an empty object. The real Neuro client
// omits `data` entirely for actions whose schema has no properties, which
// reaches the plugin as an empty string.
bool IsEmptyJsonObject(std::string const &data);

// `key` must include its quotes, e.g. "\"index\"".
bool ParseJsonInteger(std::string const &json, char const *key, int &out);

// Accepts only the JSON literals true/false; `out` is 1 or 0.
bool ParseJsonBoolean(std::string const &json, char const *key, int &out);
