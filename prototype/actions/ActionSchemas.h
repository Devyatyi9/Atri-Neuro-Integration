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
// `line` counts the lines currently on screen, from the top. The numbering is
// window-relative: after scrolling the same number is a different line.
// `from` is optional: omitted means the most recent lines. Numbered from 1
// over the whole history, unlike the window-relative line numbers above.
// Optional language on both readers: omitted or "displayed" gives what is on
// screen, "japanese" gives the original column of the language table.
constexpr char const *const kReadLineSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"language\":{\"type\":\"string\","
    "\"enum\":[\"displayed\",\"japanese\"]}},"
    "\"additionalProperties\":false}";
constexpr char const *const kReadBacklogSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"from\":{\"type\":\"integer\",\"minimum\":1},"
    "\"language\":{\"type\":\"string\","
    "\"enum\":[\"displayed\",\"japanese\"]}},"
    "\"additionalProperties\":false}";
// The Extra screen and the backlog rows have no constant schemas: their
// maximums are what is on screen at that moment -- squares on this page,
// pages in this gallery, tracks in this list, rows in this window -- so the
// router builds them per registration through BoundedIntSchema.
constexpr char const *const kGalleryViewSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kMusicScrollSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kOpenExtraSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kShowSceneListSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kShowBacklogLinesSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kBacklogPageUpSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kBacklogPageDownSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kBacklogTopSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kBacklogEndSchema =
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
// `slot` counts from 1 like the numbering on the load screen; the handler
// converts to kag's 0-based bookmark index.
constexpr char const *const kReplayVoiceSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kPreviousSceneSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kNextSceneSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kOpenSaveSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
// Same shape as load: `slot` counts from 1. Only empty slots are accepted.
// Same shape again: `slot` counts from 1, over the voice list this time.
constexpr char const *const kPlayVoiceSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"slot\":{\"type\":\"integer\",\"minimum\":1}},"
    "\"required\":[\"slot\"],"
    "\"additionalProperties\":false}";
constexpr char const *const kSaveGameSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"slot\":{\"type\":\"integer\",\"minimum\":1}},"
    "\"required\":[\"slot\"],"
    "\"additionalProperties\":false}";
constexpr char const *const kLoadSaveSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"slot\":{\"type\":\"integer\",\"minimum\":1}},"
    "\"required\":[\"slot\"],"
    "\"additionalProperties\":false}";
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
