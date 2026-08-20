#pragma once

// JSON schemas advertised with each action in actions/register.
//
// Kept apart from the router because they are data Neuro reads, not logic: the
// schema and the action description are the whole contract she sees, so they
// are worth reviewing as a set rather than hunting through handlers. Only the
// keywords the API specification lists as well-supported are used here (see
// neuro-sdk/API/SPECIFICATION.md).

// Actions with no parameters. Neuro omits `data` entirely for these, which
// reaches the plugin as an empty string; see IsEmptyJsonObject.
constexpr char const *const kContinueGameSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kAdvanceTextSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kGoBackSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kOpenMenuSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kOpenQuickMenuSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kCloseQuickMenuSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kReturnToTitleSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kHideTextWindowSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kShowTextWindowSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kOpenLoadSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kOpenBacklogSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kStartAutoModeSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kStopAutoModeSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kStartSkipModeSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kStopSkipModeSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";

// `index` counts from 1 so it matches the numbering of the choice list in the
// context message ("1) ... 2) ..."); the handler converts to the engine's
// 0-based array.
constexpr char const *const kSelectChoiceSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"index\":{\"type\":\"integer\",\"minimum\":1}},"
    "\"required\":[\"index\"],"
    "\"additionalProperties\":false}";
constexpr char const *const kAnswerDialogSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"confirm\":{\"type\":\"boolean\"}},"
    "\"required\":[\"confirm\"],"
    "\"additionalProperties\":false}";
