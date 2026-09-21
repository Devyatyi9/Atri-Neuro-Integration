#pragma once

// JSON schemas advertised with each action in actions/register.
//
// Kept apart from the router because they are data Neuro reads, not logic: the
// schema and the action description are the whole contract she sees, so they
// are worth reviewing as a set rather than hunting through handlers.
//
// `additionalProperties` is deliberate, and deliberately an exception: the API
// specification lists it among the keywords Neuro probably does not support,
// and Tony warns about it on every register. It is kept because the tooling
// this is tested with does use it -- Gary refuses to put anything in the body
// of an action whose schema forbids extra properties, by hand or through the
// randomiser -- and because it costs nothing where it is ignored (Randy feeds
// the schema to json-schema-faker, which never invents extra fields anyway).
// Do not remove it to silence the warning.

// Actions with no parameters. Neuro omits `data` entirely for these, which
// reaches the plugin as an empty string; see IsEmptyJsonObject.
constexpr char const *const kContinueGameSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kAdvanceTextSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kSkipMovieSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kGoBackSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kOpenSettingsSchema =
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
constexpr char const *const kReadShortcutsSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kHideUiSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kShowUiSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kConfirmLanguageSchema =
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
// settings_tab is built the same way, from the tab count of the screen.
// The Extra screen and the backlog rows have no constant schemas: their
// maximums are what is on screen at that moment -- squares on this page,
// pages in this gallery, tracks in this list, rows in this window -- so the
// router builds them per registration through BoundedIntSchema.
constexpr char const *const kGalleryViewSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
// Moving the open picture, in whole steps of the game's own size. Both axes are
// optional: a move along one is as ordinary as a move along both. The bound is
// ours and generous -- the game clamps at the edges anyway, so a step too far
// simply stops there.
// The music player's volume, as the slider reads it.
constexpr char const *const kMusicVolumeSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"volume\":{\"type\":\"integer\","
    "\"minimum\":0,\"maximum\":100}},"
    "\"required\":[\"volume\"],"
    "\"additionalProperties\":false}";
constexpr char const *const kScrollCgSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"horizontal\":{\"type\":\"integer\","
    "\"minimum\":-64,\"maximum\":64},"
    "\"vertical\":{\"type\":\"integer\","
    "\"minimum\":-64,\"maximum\":64}},"
    "\"additionalProperties\":false}";
constexpr char const *const kMusicScrollSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kMusicScrollBarSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"position\":{\"type\":\"integer\","
    "\"minimum\":0,\"maximum\":100}},"
    "\"required\":[\"position\"],"
    "\"additionalProperties\":false}";
constexpr char const *const kStartNewGameSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kStartTrueEndSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kExitGameSchema =
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
constexpr char const *const kQuickSaveSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kQuickLoadSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
constexpr char const *const kSaveVoiceSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
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
// One shape for every slot grid: load, save and the voice collection all show
// the same ten cells. `slot` is 1 to 10 as the screen numbers them, top left
// first, so a number always means the cell on view and never an index into the
// list behind it.
constexpr char const *const kSlotSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"slot\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10}},"
    "\"required\":[\"slot\"],"
    "\"additionalProperties\":false}";
// The per-slot edit menu is opened by slot number, the same numbering as
// everything else on this screen. Its four entries take nothing: which slot
// they act on was decided when the menu was opened.
constexpr char const *const kEditMenuSchema = kSlotSchema;
// The comment is written from here rather than typed into the game's own
// window, so the text is the action's parameter. The cap is ours: the slot
// shows one short line, and refusing is better than storing something that
// cannot be read back off the screen.
constexpr char const *const kEditCommentSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"text\":{\"type\":\"string\",\"maxLength\":120}},"
    "\"required\":[\"text\"],"
    "\"additionalProperties\":false}";
constexpr char const *const kEditMenuItemSchema =
    "{\"type\":\"object\",\"properties\":{},"
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
