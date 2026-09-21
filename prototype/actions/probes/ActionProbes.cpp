#include "ActionProbes.h"

#include <windows.h>

#include <cstddef>
#include <string>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "ActionCommon.h"
#include "Logging.h"
#include "ParseNumber.h"
#include "TJSExecutor.h"
#include "TJSQuery.h"
#include "UtfConversion.h"

namespace {

// Every number in this file comes from a TJS expression, and zero is a
// meaningful answer wherever it lands: "no tabs", "empty mask", "no pages".
// atoi returned zero for a malformed answer too, so a probe that failed to
// produce a number was indistinguishable from one that honestly counted none.
// The game tolerates a slow answer far better than a wrong one, so the caller
// is told instead. The rules themselves live in ParseNumber, away from the
// game, where they can be tested.
bool ParseProbeInt(std::string const &text, int &out, char const *tag) {
	if (ParseWholeInt(text, out)) {
		return true;
	}
	Log(("[Action] " + std::string(tag) + " expected a number, got '" + text +
	     "'\n")
	        .c_str());
	return false;
}

// The recurring shape: run one probe, and treat "could not ask" the same as
// "the answer is no".
bool Ask(wchar_t const *expr, char const *tag) {
	bool value = false;
	if (!QueryBool(expr, value, tag)) {
		return false;
	}
	return value;
}

// Formats a probe that takes one substituted value and runs it. The buffer is
// sized for the longest expression below with room to spare; a truncated probe
// would be a silent wrong answer, so an overrun is reported rather than run.
bool AskFormatted(wchar_t const *format, char const *tag, ...) {
	wchar_t probe[1024];
	va_list args;
	va_start(args, tag);
	int const written = _vsnwprintf_s(probe, _TRUNCATE, format, args);
	va_end(args);
	if (written < 0) {
		Log(("[Action] " + std::string(tag) + " probe did not fit\n").c_str());
		return false;
	}
	return Ask(probe, tag);
}

std::wstring Format(wchar_t const *format, ...) {
	wchar_t text[1024];
	va_list args;
	va_start(args, format);
	int const written = _vsnwprintf_s(text, _TRUNCATE, format, args);
	va_end(args);
	return written < 0 ? std::wstring() : std::wstring(text);
}

// Hands a press to the engine's idle queue through runOnceDelayedTrigger
// instead of running it here, inside the engine's continuous callback, where a
// press that wakes the conductor re-enters it. `guard` runs now and may
// `return 0` to refuse; `press` runs later on its own, so it resolves every
// object again and returns early if the control is gone. The inner function
// sees no locals of the outer one. A shared pending flag refuses a second
// press until the first has run. The flag holds the tick it was raised at and
// counts only for a few seconds: a press the engine never ran would otherwise
// refuse every later one. A throw in the press is caught there: the engine
// calls it with nothing above to catch it.
bool AskDeferred(std::wstring const &guard,
                 std::wstring const &press,
                 char const *tag) {
	if (press.empty()) {
		Log(("[Action] " + std::string(tag) + " press did not fit\n").c_str());
		return false;
	}
	std::wstring code =
	    L"(function(){ var p = typeof global.__neuroPressPending != 'undefined'"
	    L" ? +global.__neuroPressPending : 0;"
	    L" if (p > 0 && System.getTickCount() - p < 5000) return 0;"
	    L" if (p > 0) Debug.message('[neuro] stale press flag cleared');";
	code += guard;
	code +=
	    L" runOnceDelayedTrigger(function {"
	    L" global.__neuroPressPending = 0; try { ";
	code += press;
	code +=
	    L" } catch (e) { Debug.message('[neuro] deferred press failed: ' +"
	    L" e.message); } } incontextof global);"
	    L" global.__neuroPressPending = System.getTickCount(); return 1; })()";
	return Ask(code.c_str(), tag);
}

wchar_t const *const kScreenButtonReady =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
    L"'Object' || typeof kag.current.names == 'undefined') return 0;"
    L"var button = kag.current.names['%hs'];"
    L"if (typeof button != 'Object' || !isvalid button || !button.visible || "
    L"!button.enabled) return 0;"
    L"if (typeof button.controlOwner == 'undefined' || typeof "
    L"button.controlOwner.onButtonClick != 'Object') return 0;"
    L"return 1; })()";

// The button's own enter handler, which is what the game runs when a cursor
// arrives: it lights the button up and evaluates the link's onenter expression,
// so the hover sound plays and whatever that button reveals appears.
//
// Not setFocusToLink: that moves the real system cursor, which only registers
// while the game window is active and fights whoever else is using the mouse.
// Calling the handler works with the window in the background and leaves the
// cursor where it is.
wchar_t const *const kScreenButtonHover =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
    L"'Object' || typeof kag.current.names == 'undefined') return 0;"
    L"var button = kag.current.names['%hs'];"
    L"if (typeof button != 'Object' || !isvalid button || !button.visible) "
    L"return 0;"
    L"button.%hs(); return 1; })()";

wchar_t const *const kScreenButtonPress =
    L"var button = kag.current.names['%hs'];"
    L" if (typeof button != 'Object' || !isvalid button) return;"
    L" button.controlOwner.onButtonClick(button.linkNum);";

wchar_t const *const kQuickMenuButtonReady =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.QuickMenu != "
    L"'Object') return 0;"
    L"var button = kag.QuickMenu.names['%hs'];"
    L"if (typeof button != 'Object' || !isvalid button || !button.visible || "
    L"!button.enabled) return 0;"
    L"if (typeof button.controlOwner == 'undefined' || typeof "
    L"button.controlOwner.onButtonClick != 'Object') return 0;"
    L"return 1; })()";

wchar_t const *const kQuickMenuButtonHover =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.QuickMenu != "
    L"'Object') return 0;"
    L"var button = kag.QuickMenu.names['%hs'];"
    L"if (typeof button != 'Object' || !isvalid button) return 0;"
    L"button.%hs(); return 1; })()";

// The edit buttons come from kag.current.buttons, not kag.current.names, and
// they are clicked differently as well: they carry no linkNum, so the call that
// works everywhere else -- controlOwner.onButtonClick(linkNum) -- reaches them
// as a press of nothing. Their own onButtonClick, with no argument, is the one
// that opens the menu.
wchar_t const *const kEditButtonReady =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
    L"'Object' || typeof kag.current.buttons == 'undefined') return 0;"
    L"var button = kag.current.buttons['edit%d'];"
    L"if (typeof button != 'Object' || !isvalid button || !button.visible || "
    L"!button.enabled) return 0;"
    L"if (typeof button.onButtonClick != 'Object') return 0;"
    L"return 1; })()";

wchar_t const *const kEditButtonHover =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
    L"'Object' || typeof kag.current.buttons == 'undefined') return 0;"
    L"var button = kag.current.buttons['edit%d'];"
    L"if (typeof button != 'Object' || !isvalid button || !button.visible) "
    L"return 0;"
    L"button.%hs(); return 1; })()";

wchar_t const *const kEditButtonPress =
    L"var button = kag.current.buttons['edit%d'];"
    L" if (typeof button != 'Object' || !isvalid button) return;"
    L" button.onButtonClick();";

// The menu's entries live on the panel layer instead, but are pressed the same
// way. `delete` is the entry the screen labels Erase.
wchar_t const *const kEditMenuItemHover =
    L"(function(){ if (!kag.panelShowing || typeof kag.panelLayer != 'Object' "
    L"|| !kag.panelLayer) return 0;"
    L"var item = kag.panelLayer.names['%hs'];"
    L"if (typeof item != 'Object' || !isvalid item || !item.visible) return 0;"
    L"item.%hs(); return 1; })()";

wchar_t const *const kEditMenuItemGuard =
    L"if (!kag.panelShowing || typeof kag.panelLayer != 'Object' "
    L"|| !kag.panelLayer) return 0;"
    L" var item = kag.panelLayer.names['%hs'];"
    L" if (typeof item != 'Object' || !isvalid item || !item.visible || "
    L"!item.enabled) return 0;"
    L" if (typeof item.controlOwner == 'undefined' || typeof "
    L"item.controlOwner.onButtonClick != 'Object') return 0;";

wchar_t const *const kEditMenuItemPress =
    L"if (!kag.panelShowing || typeof kag.panelLayer != 'Object' "
    L"|| !kag.panelLayer) return;"
    L" var item = kag.panelLayer.names['%hs'];"
    L" if (typeof item != 'Object' || !isvalid item) return;"
    L" item.controlOwner.onButtonClick(item.linkNum);";

// The dialog's own buttons. Same shape as everywhere else, but reached through
// kag.currentDialog rather than the screen.
wchar_t const *const kDialogLinkHover =
    L"(function(){ var d = kag.currentDialog;"
    L"if (typeof d != 'Object' || !d) return 0;"
    L"var b = d.names['%hs'];"
    L"if (typeof b != 'Object' || !isvalid b || !b.visible) return 0;"
    L"if (typeof b.%hs != 'Object') return 0;"
    L"b.%hs(); return 1; })()";

wchar_t const *const kQuickMenuButtonPress =
    L"var button = kag.QuickMenu.names['%hs'];"
    L" if (typeof button != 'Object' || !isvalid button) return;"
    L" button.controlOwner.onButtonClick(button.linkNum);";

}  // namespace

bool ProbeStateIs(char const *expected, char const *tag) {
	std::string state;
	if (!QueryString(L"typeof SystemHook != 'undefined' ? "
	                 L"SystemHook.currentState : ''",
	                 state, tag)) {
		return false;
	}
	return state == expected;
}

bool ProbeStateIsSettingsScreen(char const *tag) {
	std::string state;
	if (!QueryString(L"typeof SystemHook != 'undefined' ? "
	                 L"SystemHook.currentState : ''",
	                 state, tag)) {
		return false;
	}
	// The settings screen and the two pages that carry their own state. The
	// other tabs live inside "option" and do not change it.
	return state == "option" || state == "soundmode" || state == "voicemode";
}

bool ProbeScenarioIs(char const *expected, char const *tag) {
	std::string storage;
	if (!QueryString(L"(function(){ try { return '' + kag.conductor.curStorage; }"
	                 L" catch (e) { return ''; } })()",
	                 storage, tag)) {
		return false;
	}
	return storage == expected;
}

bool ProbeStateIsMenuScreen(char const *tag) {
	std::string state;
	if (!QueryString(L"typeof SystemHook != 'undefined' ? "
	                 L"SystemHook.currentState : ''",
	                 state, tag)) {
		return false;
	}
	// Every value the system scenarios jump to, minus title and game, collected
	// from the sysjump/syscurrent tags in new-extract-data/sysscn/*.ks -- plus
	// "voice", which that sweep cannot see because nothing jumps to it. It is the
	// Voice tab inside the save/load screen, and a different screen from
	// "voicemode", the voice settings page.
	//
	// So this list is a lower bound, not the full set. Nothing depends on it
	// being complete: the capability refresh resolves screens by the controls
	// they carry, and this only guards the go_back handler.
	return state == "option" || state == "load" || state == "save" ||
	       state == "backlog" || state == "cgmode" || state == "scenemode" ||
	       state == "soundmode" || state == "voicemode" || state == "voice" ||
	       state == "search";
}

bool ProbeScreen(ScreenSnapshot &out) {
	std::string probe;
	bool const executed = ExecuteTJSSync(
	    L"(function(){ var s = typeof SystemHook != 'undefined' ? "
	    L"SystemHook.currentState : '';"
	    // Test for 'Object', not for 'undefined': kag.currentDialog exists as a
	    // member and simply holds void when no dialog is up, and typeof reports
	    // "void" for that -- an "undefined" test passes and the following
	    // isvalid/property access then throws on the void value.
	    L"var d = 0, c = 0, p = 0, h = 0, a = 0, s2 = 0;"
	    L"var tr = 0, cv = 0, ce = 0, xv = 0, xe = 0, ev = 0, pn = 0, em = 0;"
	    L"var qv = 0;"
	    L"var sg = '';"
	    L"if (typeof kag != 'undefined') {"
	    L"if (typeof kag.currentDialog == 'Object' && kag.currentDialog.visible) "
	    L"d = 1;"
	    L"if (kag.selectShowing) c = 1;"
	    // Both readings, because they can disagree. `_dropdown` says the bar was
	    // told to slide out; `visible` says the layer is on screen at all. In
	    // ordinary play the layer is visible whether the bar is out or not, so
	    // hovering brings it down -- but a scene entered through the engine's
	    // own start point leaves the layer hidden, and then the bar can be
	    // "out" with nothing to see.
	    L"if (typeof kag.QuickMenu == 'Object') {"
	    L"if (kag.QuickMenu._dropdown) p = 1;"
	    L"if (kag.QuickMenu.visible) qv = 1; }"
	    L"if (kag.messageLayerHiding) h = 1;"
	    L"if (kag.autoMode) a = 1;"
	    // skipMode is a level, not a flag: 1 and 2 are the momentary states of an
	    // ordinary click, and only 3 (SKIP_STOP) and above are the mode a player
	    // turns on. The quick menu bar tests it the same way.
	    L"if (kag.skipMode >= 3) s2 = 1;"
	    // The title's buttons, visibility and usability separately. Neither
	    // reading includes the controlOwner pair ProbeScreenButtonReady also
	    // checks: these only decide what to advertise, and the handlers run the
	    // full probe before pressing anything.
	    L"if (typeof kag.current == 'Object' && typeof kag.current.names != "
	    L"'undefined') {"
	    L"var t = kag.current.names;"
	    L"if (typeof t['load'] == 'Object' && t['load'].visible && "
	    L"t['load'].enabled) tr = 1;"
	    L"if (typeof t['cont'] == 'Object' && t['cont'].visible) {"
	    L"cv = 1; if (t['cont'].enabled) ce = 1; }"
	    L"if (typeof t['extra'] == 'Object' && t['extra'].visible) {"
	    L"xv = 1; if (t['extra'].enabled) xe = 1; }"
	    L"if (typeof t['tend'] == 'Object' && t['tend'].visible) ev = 1;"
	    L"}"
	    // A panel over the screen, and separately whether it is the per-slot edit
	    // menu with its entries built. Other screens raise panels of their own,
	    // so the entry test is what identifies this one.
	    L"if (kag.panelShowing) { pn = 1;"
	    L"if (typeof kag.panelLayer == 'Object' && kag.panelLayer) {"
	    L"try { if (typeof kag.panelLayer.names.comment == 'Object') em = 1; }"
	    L"catch (e) { } } }"
	    // Which scenario the screen is running. Read from the active conductor,
	    // not from kag.currentStorage: a menu opened during play runs on the
	    // extra conductor, and currentStorage keeps naming the scene behind it.
	    L"try { sg = '' + kag.conductor.curStorage; } catch (e) { sg = ''; }"
	    L"}"
	    L"return s + '|' + d + '|' + c + '|' + p + '|' + h + '|' + a + '|' + s2"
	    L"+ '|' + tr + '|' + cv + '|' + ce + '|' + xv + '|' + xe + '|' + ev"
	    L"+ '|' + pn + '|' + em + '|' + qv + '|' + sg; })()",
	    &probe, 5000, TJSExecMode::Expression);
	if (!executed) {
		Log("[Action] Refresh state probe failed\n");
		return false;
	}

	// "<state>", then fifteen single-digit flags each behind a bar, then the
	// scenario storage. Neither string contains a bar, so the flags sit at fixed
	// offsets from the first one and the storage is whatever follows them. The
	// width check is what catches a flag added to the expression and not here.
	size_t const separator = probe.find('|');
	size_t const flagsEnd = separator + 30;
	if (separator == std::string::npos || probe.size() <= flagsEnd ||
	    probe[flagsEnd] != '|') {
		Log("[Action] Refresh state probe returned an unexpected shape\n");
		return false;
	}
	out.state = probe.substr(0, separator);
	out.dialogVisible = probe[separator + 1] == '1';
	out.choiceVisible = probe[separator + 3] == '1';
	out.quickMenuOut = probe[separator + 5] == '1';
	out.textWindowHidden = probe[separator + 7] == '1';
	out.autoMode = probe[separator + 9] == '1';
	out.skipMode = probe[separator + 11] == '1';
	out.titleReady = probe[separator + 13] == '1';
	out.titleShowsContinue = probe[separator + 15] == '1';
	out.titleCanContinue = probe[separator + 17] == '1';
	out.titleShowsExtra = probe[separator + 19] == '1';
	out.titleCanOpenExtra = probe[separator + 21] == '1';
	out.titleShowsTrueEnd = probe[separator + 23] == '1';
	out.panelShowing = probe[separator + 25] == '1';
	out.editMenuReady = probe[separator + 27] == '1';
	out.quickMenuVisible = probe[separator + 29] == '1';
	out.storage = probe.substr(flagsEnd + 1);
	return true;
}

bool ProbeScreenButtonReady(char const *button, char const *tag) {
	return AskFormatted(kScreenButtonReady, tag, button);
}

bool DispatchScreenButtonHover(char const *button, bool on, char const *tag) {
	return AskFormatted(kScreenButtonHover, tag, button,
	                    on ? "onMouseEnter" : "onMouseLeave");
}

bool ProbeScreenButtonHovered(char const *button, char const *tag) {
	// Whether the game actually took the cursor onto the button. Moving it is not
	// enough on its own: the hit test runs on the game's own mouse handling, and
	// with its window in the background nothing registers.
	return AskFormatted(
	    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
	    L"'Object' || typeof kag.current.names == 'undefined') return 0;"
	    L"var button = kag.current.names['%hs'];"
	    L"if (typeof button != 'Object' || !isvalid button) return 0;"
	    L"return button.Butt_mouseOn ? 1 : 0; })()",
	    tag, button);
}

bool DispatchScreenButton(char const *button, char const *tag) {
	// A press a player makes is always preceded by the cursor arriving: the
	// button lights up, its hover sound plays, and whatever it reveals appears
	// for a moment before it is pressed. Doing the same here is what makes the
	// press look like a press rather than a value changing on its own.
	if (AskFormatted(kScreenButtonHover, tag, button, "onMouseEnter")) {
		Sleep(kHoverPreviewMs);
	}
	// And the cursor leaves again, just before the press that is queued behind
	// it. A press that changes the screen takes the button with it, but one that
	// leaves the screen standing -- Back cancelling a copy, a tab that stays --
	// left the button lit for good, because nothing here has a cursor that moves
	// away on its own.
	AskFormatted(kScreenButtonHover, tag, button, "onMouseLeave");
	return AskDeferred(L"", Format(kScreenButtonPress, button), tag);
}

bool ProbeLanguageScreen(char const *tag) {
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current "
	    L"!= 'Object' || typeof kag.current.names == 'undefined') return 0;"
	    L"if (typeof kag.current.names['lang_text'] != 'Object') return 0;"
	    L"if (typeof kag.current.names['title'] == 'Object') return 0;"
	    L"if (typeof kag.current.names['page1'] == 'Object') return 0;"
	    L"return 1; })()",
	    tag);
}

bool ProbeCanContinue(char const *tag) {
	return Ask(
	    L"typeof SystemAction != 'undefined' && SystemAction.canContinue "
	    L"? 1 : 0",
	    tag);
}

// This runs from the engine's continuous-event callback, so the click itself is
// handed to runOnceDelayedTrigger instead of being made here: clicking in place
// lets the conductor re-enter from inside that callback. The four readiness
// checks are the ones the conductor applies to its own click wait. A pending
// flag keeps a second request from queueing another click before the first has
// run; the deferred function clears it before clicking, and leaving the story
// clears it too. It holds the tick it was raised at and lapses after a few
// seconds, so a click the engine never ran cannot hold it up for the rest of
// the scene.
AdvanceResult AdvanceDialogue(char const *tag) {
	std::string answer;
	if (!QueryString(
	        L"(function(){"
	        L" if (typeof SystemHook == 'undefined' || "
	        L"SystemHook.currentState != 'game') {"
	        L" global.__neuroAdvancePending = false; return 2; }"
	        L" if (typeof kag == 'undefined' || typeof "
	        L"kag.onPrimaryClick != 'Object') return 2;"
	        L" if (typeof kag.isMoviePlaying == 'Object' && "
	        L"kag.isMoviePlaying()) return 3;"
	        L" var p = typeof global.__neuroAdvancePending != 'undefined'"
	        L" ? +global.__neuroAdvancePending : 0;"
	        L" if (p > 0 && System.getTickCount() - p < 5000) return 4;"
	        L" if (p > 0) Debug.message('[neuro] stale advance flag cleared');"
	        L" if (typeof kag.clickWaiting == 'undefined' || "
	        L"!kag.clickWaiting) return 4;"
	        L" if (typeof kag.inStable != 'undefined' && !kag.inStable)"
	        L" return 4;"
	        L" var c = kag.conductor;"
	        L" if (c.status != c.mWait || c.waitUntil['click'] === void)"
	        L" return 4;"
	        L" runOnceDelayedTrigger(function {"
	        L" global.__neuroAdvancePending = 0;"
	        L" kag.onPrimaryClick(); } incontextof global);"
	        L" global.__neuroAdvancePending = System.getTickCount();"
	        L" return 1; })()",
	        answer, tag)) {
		return AdvanceResult::Unavailable;
	}
	int code = 0;
	if (!ParseWholeInt(answer, code)) {
		Log(("[Action] " + std::string(tag) + " expected a code, got '" + answer +
		     "'\n")
		        .c_str());
		return AdvanceResult::Unavailable;
	}
	switch (code) {
		case 1:
			return AdvanceResult::Advanced;
		case 2:
			return AdvanceResult::NotInStory;
		case 3:
			return AdvanceResult::MoviePlaying;
		case 4:
			return AdvanceResult::NothingWaiting;
		default:
			return AdvanceResult::Unavailable;
	}
}

bool ProbeClickWaiting(char const *tag) {
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.onPrimaryClick != 'Object') return 0;"
	    L"if (typeof kag.clickWaiting == 'undefined' || "
	    L"!kag.clickWaiting) return 0;"
	    L"return 1; })()",
	    tag);
}

bool DispatchPrimaryClick(char const *tag) {
	return Ask(L"(function(){ kag.onPrimaryClick(); return 1; })()", tag);
}

bool ProbeChoiceLayerReady(char const *tag) {
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.selectLayer == 'undefined') return 0;"
	    L"var layer = kag.selectLayer;"
	    L"if (layer.selects == void || layer.selects.count < 1) return 0;"
	    L"if (typeof layer.enabled != 'undefined' && !layer.enabled) "
	    L"return 0;"
	    L"return 1; })()",
	    tag);
}

bool ProbeChoiceButtonReady(int index, char const *tag) {
	return AskFormatted(
	    L"(function(){ if (typeof kag == 'undefined' || typeof kag.selectLayer "
	    L"== 'undefined') return 0;"
	    L"var layer = kag.selectLayer;"
	    L"if (layer.selects == void || layer.selects.count <= %d) return 0;"
	    L"var button = layer.selects[%d];"
	    L"if (!isvalid button || (typeof button.enabled != 'undefined' && "
	    L"!button.enabled)) return 0;"
	    L"return 1; })()",
	    tag, index, index);
}

bool DispatchChoice(int index, char const *tag) {
	// The same arrival a player makes before pressing: the button takes its
	// hover look and holds it for a moment. These buttons have no hover
	// handler to call -- the engine drives the state itself from a flag -- so
	// the flag is raised and the button asked to redraw.
	if (AskFormatted(L"(function(){ var layer = kag.selectLayer;"
	                 L"var button = layer.selects[%d];"
	                 L"if (!isvalid button) return 0;"
	                 L"button.Butt_mouseOn = 1; button.update(); return 1; })()",
	                 tag, index)) {
		Sleep(kHoverPreviewMs);
	}
	// Nothing is sent to leave again: the choice disappears with the press,
	// taking its buttons along. The press goes through runOnceDelayedTrigger
	// for the same reason as the advance click: made in place, it runs the
	// conductor from inside the engine's callback. The button is checked again
	// here, after the hover pause, and once more when the press runs. A pending
	// flag keeps a second request from queueing another press; it is cleared by
	// the press itself, by any request that finds the choice gone, and lapses
	// on its own after a few seconds.
	return AskFormatted(
	    L"(function(){ var layer = kag.selectLayer;"
	    L" if (layer.selects == void || layer.selects.count <= %d) {"
	    L" global.__neuroChoicePending = 0; return 0; }"
	    L" var p = typeof global.__neuroChoicePending != 'undefined'"
	    L" ? +global.__neuroChoicePending : 0;"
	    L" if (p > 0 && System.getTickCount() - p < 5000) return 0;"
	    L" if (p > 0) Debug.message('[neuro] stale choice flag cleared');"
	    L" if (!isvalid layer.selects[%d]) return 0;"
	    L" runOnceDelayedTrigger(function {"
	    L" global.__neuroChoicePending = 0;"
	    L" var l = kag.selectLayer;"
	    L" if (l.selects == void || l.selects.count <= %d) return;"
	    L" var b = l.selects[%d]; if (isvalid b) l.onButtonClick(b); }"
	    L" incontextof global);"
	    L" global.__neuroChoicePending = System.getTickCount();"
	    L" return 1; })()",
	    tag, index, index, index, index);
}

bool ProbeQuickMenuCanDropDown(char const *tag) {
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.QuickMenu != 'Object') return 0;"
	    L"if (typeof kag.QuickMenu.updown != 'Object') return 0;"
	    L"if (!kag.QuickMenu.canDropDown) return 0;"
	    L"return 1; })()",
	    tag);
}

bool ProbeQuickMenuCameOut(char const *tag) {
	// Asked after the press, not before it. A hidden layer answers canDropDown
	// and sets _dropdown when told to, so the only honest way to know the bar
	// arrived is to look at it afterwards -- which is also what the player does.
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.QuickMenu != 'Object') return 0;"
	    L"if (!kag.QuickMenu.visible) return 0;"
	    L"return kag.QuickMenu._dropdown ? 1 : 0; })()",
	    tag);
}

bool ProbeQuickMenuHasUpdown(char const *tag) {
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.QuickMenu != 'Object') return 0;"
	    L"if (typeof kag.QuickMenu.updown != 'Object') return 0;"
	    L"return 1; })()",
	    tag);
}

bool DispatchQuickMenuOpen(char const *tag) {
	return Ask(
	    L"(function(){ kag.QuickMenu.updown(1); return "
	    L"kag.QuickMenu._dropdown ? 1 : 0; })()",
	    tag);
}

bool DispatchQuickMenuClose(char const *tag) {
	return Ask(
	    L"(function(){ kag.QuickMenu.updown(0); return "
	    L"kag.QuickMenu._dropdown ? 0 : 1; })()",
	    tag);
}

bool ProbeQuickMenuButtonReady(char const *button, char const *tag) {
	return AskFormatted(kQuickMenuButtonReady, tag, button);
}

bool DispatchQuickMenuButton(char const *button, char const *tag) {
	// Same as the screen buttons: the bar's own hover sound is se901, which is
	// the sound the bar has always seemed to make when it slides out.
	if (AskFormatted(kQuickMenuButtonHover, tag, button, "onMouseEnter")) {
		Sleep(kHoverPreviewMs);
	}
	AskFormatted(kQuickMenuButtonHover, tag, button, "onMouseLeave");
	return AskDeferred(L"", Format(kQuickMenuButtonPress, button), tag);
}

bool DispatchShowTextWindow(char const *tag) {
	// showMessageLayerByUser() is what the engine reaches when the player clicks
	// or presses Escape while the window is away; it clears messageLayerHiding
	// and re-triggers the conductor. There is no control to route through here:
	// the bar's own hide button reports enabled = 0 once the window is gone.
	return AskDeferred(
	    L"if (typeof kag == 'undefined' || typeof "
	    L"kag.showMessageLayerByUser != 'Object' || "
	    L"!kag.messageLayerHiding) return 0;",
	    L"kag.showMessageLayerByUser();", tag);
}

bool ProbeBookmarkExists(int number, char const *tag) {
	return AskFormatted(
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.isExistentBookMark != 'Object') return 0;"
	    L"if (%d < 0 || %d >= kag.numBookMarks) return 0;"
	    L"return kag.isExistentBookMark(%d) ? 1 : 0; })()",
	    tag, number, number, number);
}

bool ProbeLoadAccepted(char const *tag) {
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined') return 0;"
	    L"if (typeof kag.currentDialog == 'Object' && "
	    L"kag.currentDialog.visible) return 1;"
	    L"if (typeof SystemHook != 'undefined' && "
	    L"SystemHook.currentState == 'game') return 1;"
	    L"return 0; })()",
	    tag);
}

bool ProbeSaveTabs(int &visibleMask, int &enabledMask) {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof kag == 'undefined' || typeof kag.current "
	        L"!= 'Object' || typeof kag.current.names == 'undefined') return '';"
	        L"var n = kag.current.names;"
	        // Bit order is the order the tabs sit on screen, left to right.
	        L"var names = ['to_save', 'to_load', 'to_quick', 'to_voice'];"
	        L"var v = 0, e = 0;"
	        L"for (var i = 0; i < names.count; i++) {"
	        L"var b = n[names[i]];"
	        L"if (typeof b == 'Object' && b.visible) {"
	        L"v = v | (1 << i); if (b.enabled) e = e | (1 << i); } }"
	        L"return v + '|' + e; })()",
	        raw, "save tabs")) {
		return false;
	}
	size_t const separator = raw.find('|');
	if (separator == std::string::npos) {
		return false;
	}
	return ParseProbeInt(raw.substr(0, separator), visibleMask, "save tabs") &&
	       ParseProbeInt(raw.substr(separator + 1), enabledMask, "save tabs");
}

bool ProbeEditButtonReady(int position, char const *tag) {
	return AskFormatted(kEditButtonReady, tag, position);
}

bool DispatchEditButton(int position, char const *tag) {
	// Hovering first, the way every other press on every screen does it: the
	// button lights up, and it stays lit long enough to be seen before the
	// click. And leaving after, so nothing stays lit once the press is done.
	if (AskFormatted(kEditButtonHover, tag, position, "onMouseEnter")) {
		Sleep(kHoverPreviewMs);
	}
	AskFormatted(kEditButtonHover, tag, position, "onMouseLeave");
	return AskDeferred(L"", Format(kEditButtonPress, position), tag);
}

bool ProbeEditMenuItems(int &visibleMask, int &enabledMask, char const *tag) {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (!kag.panelShowing || typeof kag.panelLayer != "
	        L"'Object' || !kag.panelLayer) return '';"
	        L"var names = ['comment', 'copy', 'move', 'delete'];"
	        L"var v = 0, e = 0;"
	        L"for (var i = 0; i < 4; i++) {"
	        L"var item = kag.panelLayer.names[names[i]];"
	        L"if (typeof item == 'Object' && isvalid item && item.visible) {"
	        L"v = v | (1 << i); if (item.enabled) e = e | (1 << i); } }"
	        L"return v + '|' + e; })()",
	        raw, tag) ||
	    raw.empty()) {
		return false;
	}
	size_t const separator = raw.find('|');
	if (separator == std::string::npos) {
		return false;
	}
	return ParseProbeInt(raw.substr(0, separator), visibleMask, tag) &&
	       ParseProbeInt(raw.substr(separator + 1), enabledMask, tag);
}

bool DispatchEditMenuItemHover(char const *item, bool on, char const *tag) {
	return AskFormatted(kEditMenuItemHover, tag, item,
	                    on ? "onMouseEnter" : "onMouseLeave");
}

bool DispatchEditMenuItem(char const *item, char const *tag) {
	if (AskFormatted(kEditMenuItemHover, tag, item, "onMouseEnter")) {
		Sleep(kHoverPreviewMs);
	}
	AskFormatted(kEditMenuItemHover, tag, item, "onMouseLeave");
	return AskDeferred(Format(kEditMenuItemGuard, item),
	                   Format(kEditMenuItemPress, item), tag);
}

bool DispatchCloseEditMenu(char const *tag) {
	return AskDeferred(
	    L"if (!kag.panelShowing || typeof kag.panelLayer != "
	    L"'Object' || !kag.panelLayer) return 0;",
	    L"if (kag.panelShowing && typeof kag.panelLayer == 'Object' && "
	    L"kag.panelLayer) kag.panelLayer.close();",
	    tag);
}

bool ProbeEditMenuSlot(int &position, char const *tag) {
	std::string raw;
	if (!QueryString(L"(function(){ try { return '' + (int)tf.SaveDataEdit; }"
	                 L"catch (e) { return ''; } })()",
	                 raw, tag) ||
	    raw.empty()) {
		return false;
	}
	return ParseProbeInt(raw, position, tag);
}

bool ProbeSlotComment(int position, std::string &out, char const *tag) {
	std::wstring code =
	    L"(function(){ var s = global.__cm_saveStore;"
	    L"if (typeof s != 'Object' || !isvalid s) return '';"
	    L"try { var num = s.withOffset(";
	code += std::to_wstring(position);
	code +=
	    L"); var text = s.getCommentText(num, null, 1);"
	    L"return (text === void) ? '' : '' + text; }"
	    L"catch (e) { return ''; } })()";
	// An empty answer is a save with no comment, which is a real answer and not
	// a failed probe -- QueryString says which by its return value.
	return QueryString(code.c_str(), out, tag);
}

bool ProbeCommentWindowOpen(char const *tag) {
	return Ask(
	    L"(function(){ var s = global.__cm_saveStore;"
	    L"if (typeof s != 'Object' || !isvalid s) return 0;"
	    L"try { return s._inEditComment ? 1 : 0; } catch (e) { return 0; }"
	    L"})()",
	    tag);
}

namespace {

// The text goes into a TJS string literal, so everything TJS would read as
// something other than a character has to be spelled out. Newlines included:
// the literal cannot span lines, and a comment arriving with one would
// otherwise break the whole expression rather than the one field.
std::wstring EscapeForTJSLiteral(std::wstring const &text) {
	std::wstring escaped;
	escaped.reserve(text.size() + 8);
	for (wchar_t const character : text) {
		switch (character) {
			case L'\"':
				escaped += L"\\\"";
				break;
			case L'\\':
				escaped += L"\\\\";
				break;
			case L'\n':
				escaped += L"\\n";
				break;
			case L'\r':
				escaped += L"\\r";
				break;
			case L'\t':
				escaped += L"\\t";
				break;
			default:
				escaped += character;
				break;
		}
	}
	return escaped;
}

}  // namespace

bool DispatchSetComment(int position,
                        std::string const &text,
                        char const *tag) {
	std::wstring wide;
	if (!Utf8ToUtf16(text.c_str(), text.size(), wide)) {
		Log(("[Action] " + std::string(tag) + " could not convert the text\n")
		        .c_str());
		return false;
	}

	// Everything the game does after its own comment window returns a string:
	// store the text, redraw the slot, and tell the screen it changed. Leaving
	// any of the three out shows stale text until something else redraws.
	std::wstring code =
	    L"(function(){ var s = global.__cm_saveStore;"
	    L"if (typeof s != 'Object' || !isvalid s) return 0;"
	    L"try { var num = s.withOffset(";
	code += std::to_wstring(position);
	code +=
	    L"); var info = s.getItemInfo(num);"
	    L"if (info === void) return 0;"
	    L"s.setCommentText(num, \"";
	code += EscapeForTJSLiteral(wide);
	code +=
	    L"\"); s.forceRedrawItem(num);"
	    L"s.onStateChanged('comment', num, info);"
	    // Read back rather than assume. kag.writeBookMarkInfo, which is where
	    // this ends up, checks the save file before touching it and does
	    // nothing at all when the check fails -- silently, with no error to
	    // catch. Without this, a comment that was never written would still
	    // be reported as written.
	    L"var back = s.getCommentText(num, null, 1);"
	    L"return back == \"";
	code += EscapeForTJSLiteral(wide);
	code +=
	    L"\" ? 1 : 0; }"
	    L"catch (e) { return 0; } })()";
	return Ask(code.c_str(), tag);
}

bool ProbeCopyMode(int &mode, char const *tag) {
	std::string raw;
	if (!QueryString(L"(function(){ var s = global.__cm_saveStore;"
	                 L"if (typeof s != 'Object' || !isvalid s) return '';"
	                 L"try { var m = '' + s._iscopy;"
	                 L"return m == 'copy' ? '1' : m == 'move' ? '2' : "
	                 L"m == 'swap' ? '3' : '0'; } catch (e) { return ''; } })()",
	                 raw, tag) ||
	    raw.empty()) {
		return false;
	}
	return ParseProbeInt(raw, mode, tag);
}

bool ProbeSaveWindow(int &offset, int &page) {
	// Every access is wrapped: the instance is invalidated the moment the screen
	// closes, and touching it then throws rather than returning void.
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof global.__cm_saveStore != 'Object') "
	        L"return '';"
	        L"try { return global.__cm_saveStore.offset + '|' + "
	        L"global.__cm_saveStore.page; } catch (e) { return ''; } })()",
	        raw, "save window")) {
		return false;
	}
	size_t const separator = raw.find('|');
	if (separator == std::string::npos) {
		return false;
	}
	return ParseProbeInt(raw.substr(0, separator), offset, "save window") &&
	       ParseProbeInt(raw.substr(separator + 1), page, "save window");
}

bool ProbeVoiceSaveMode(char const *tag) {
	return Ask(
	    L"(function(){ try { return typeof tf.VoiceSave == 'Object' "
	    L"? 1 : 0; } catch (e) { return 0; } })()",
	    tag);
}

bool ProbeVoiceExists(int index, char const *tag) {
	return AskFormatted(
	    L"(function(){ if (typeof global.__cm_saveStore != 'Object') return 0;"
	    L"try { return global.__cm_saveStore.isExistData(%d) ? 1 : 0; }"
	    L"catch (e) { return 0; } })()",
	    tag, index);
}

bool ProbeVoiceLine(int index, std::string &out) {
	wchar_t probe[512];
	// Speaker, scene and the line itself, as the record holds them. The text is
	// one frozen string: it was written in whatever language the game was in
	// when the line was kept, and nothing here can ask for another. The record
	// has a `lang` field, but it names the language column of that moment, not
	// the language of the string -- on a patched profile every entry reads
	// lang = 1 while some of them hold Japanese -- so it is not reported.
	//
	// The last field is the "New" badge, which this screen puts on the entry
	// written last exactly as the save list does. It lives on the store, not on
	// the record.
	if (_snwprintf_s(
	        probe, _TRUNCATE,
	        L"(function(){ var st = global.__cm_saveStore;"
	        L"if (typeof st != 'Object') return '';"
	        // The line is the game's own text and the record is tab-separated:
	        // a tab or a newline inside it would shift every field after it.
	        L"var flat = function(v) { var t = '' + v; var a = [];"
	        L" a.split('\\t', t); t = a.join(' ');"
	        L" a = []; a.split('\\n', t); t = a.join(' ');"
	        L" a = []; a.split('\\r', t); return a.join(' '); };"
	        L"try { var d = st.getItemInfo(%d);"
	        L"if (typeof d != 'Object') return '';"
	        L"return flat(d.name) + '\\t' + flat(d.scene) + '\\t' + "
	        L"flat(d.text) + '\\t' + (st.lastSaved == %d ? '1' : '0'); }"
	        L"catch (e) { return ''; } })()",
	        index, index) < 0) {
		Log("[Action] voice line probe did not fit\n");
		return false;
	}
	return QueryString(probe, out, "voice line");
}

bool ProbeSlotsShowBase(int base, char const *tag) {
	return AskFormatted(
	    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
	    L"'Object' || typeof kag.current.names == 'undefined') return 0;"
	    L"for (var r = 0; r < 2; r++) { for (var c = 0; c < 5; c++) {"
	    L"var b = kag.current.names['item' + r + c];"
	    L"if (typeof b != 'Object') return 0;"
	    L"var n = %d + r * 5 + c;"
	    L"var want = (n < kag.numBookMarks && kag.isExistentBookMark(n)) ? 1 : 0;"
	    L"if ((b.enabled ? 1 : 0) != want) return 0; } }"
	    L"return 1; })()",
	    tag, base);
}

bool ProbeLoadSelectionTitle(std::string &title) {
	// _lastSelect is set by the screen's own onSelect just before it asks, and
	// uses the same numbering as getItemInfo.
	return QueryString(
	           L"(function(){ var st = global.__cm_saveStore;"
	           L" if (typeof st != 'Object' || !isvalid st) return '';"
	           L" try { var n = st._lastSelect;"
	           L" if (n === void || !st.isExistData(n)) return '';"
	           L" var t = st.getItemInfo(n).title;"
	           L" return t === void ? '' : '' + t; } catch (e) { return ''; } "
	           L"})()",
	           title, "load selection title") &&
	       !title.empty();
}

bool ProbeQuickLoadTitle(std::string &title) {
	return QueryString(
	           L"(function(){ try { var n = SystemAction.qsaveBookmarkNumber;"
	           L" if (kag.getBookMarkDate(n) == '') return '';"
	           L" var t = kag.getBookMarkPageName(n);"
	           L" return t === void ? '' : '' + t; } catch (e) { return ''; } "
	           L"})()",
	           title, "quick load title") &&
	       !title.empty();
}

bool ProbeSlotDetails(int position, std::string &out) {
	wchar_t probe[1024];
	if (_snwprintf_s(
	        probe, _TRUNCATE,
	        L"(function(){ if (typeof global.__cm_saveStore != 'Object') "
	        L"return '';"
	        L"var st = global.__cm_saveStore;"
	        L"try { var idx = st.offset + %d;"
	        L"if (idx >= st.count || !st.isExistData(idx)) return '0';"
	        L"var d = st.getItemInfo(idx);"
	        // Void where a save carries no comment, and a tab-separated record
	        // cannot hold a void, so it becomes the empty field it means.
	        L"var c = st.getCommentText(idx, null, 1);"
	        L"if (c === void) c = '';"
	        // The record below is tab-separated and one line, and a comment is
	        // the player's own text: a tab or a newline in it would shift every
	        // field after it. They become spaces here, where the text is still
	        // whole.
	        L"var p1 = []; p1.split('\\t', c); c = p1.join(' ');"
	        L"var p2 = []; p2.split('\\n', c); c = p2.join(' ');"
	        L"var p3 = []; p3.split('\\r', c); c = p3.join(' ');"
	        // The screen badges one slot "New": the one the store remembers as
	        // written last. Nothing on the record itself says so.
	        L"var fresh = (st.lastSaved == idx) ? '1' : '0';"
	        L"return '1\\t' + d.title + '\\t' + d.realdate + '\\t' + c + '\\t' + "
	        L"fresh + '\\t' + d.lastText; } catch (e) { return ''; } })()",
	        position) < 0) {
		Log("[Action] slot details probe did not fit\n");
		return false;
	}
	return QueryString(probe, out, "slot details") && !out.empty();
}

bool ProbeSlotWindow(bool voice, int &base, int &page, std::string &out) {
	wchar_t probe[1024];
	if (_snwprintf_s(
	        probe, _TRUNCATE,
	        L"(function(){ if (typeof global.__cm_saveStore != 'Object') "
	        L"return '';"
	        L"var st = global.__cm_saveStore;"
	        L"try { var s = st.offset + '|' + st.page;"
	        L"for (var i = 0; i < 10; i++) {"
	        L"var idx = st.offset + i;"
	        L"s += '\n' + (i + 1) + '\t';"
	        L"if (idx < st.count && st.isExistData(idx)) {"
	        L"var d = st.getItemInfo(idx);"
	        L"s += d['%hs'] + '\t' + d['%hs']; } else { s += '\t'; } }"
	        L"return s; } catch (e) { return ''; } })()",
	        voice ? "name" : "title", voice ? "title" : "realdate") < 0) {
		Log("[Action] slot window probe did not fit\n");
		return false;
	}
	std::string raw;
	if (!QueryString(probe, raw, "slot window")) {
		return false;
	}
	size_t const firstLine = raw.find('\n');
	size_t const separator = raw.find('|');
	if (firstLine == std::string::npos || separator == std::string::npos ||
	    separator > firstLine) {
		return false;
	}
	if (!ParseProbeInt(raw.substr(0, separator), base, "slot window") ||
	    !ParseProbeInt(raw.substr(separator + 1, firstLine - separator - 1), page,
	                   "slot window")) {
		return false;
	}
	out = raw.substr(firstLine + 1);
	return true;
}

int ProbeSavePageCount() {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof kag == 'undefined' || typeof "
	        L"kag.current != 'Object' || typeof kag.current.names == "
	        L"'undefined') return '0';"
	        L"var n = 0;"
	        L"for (var i = 1; i <= 32; i++) {"
	        L"if (typeof kag.current.names['page' + i] == 'Object') n++; }"
	        L"return '' + n; })()",
	        raw, "save page count")) {
		return 0;
	}
	int count = 0;
	return ParseProbeInt(raw, count, "save page count") ? count : 0;
}

bool ProbeBacklogButtons(int &flags) {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof kag == 'undefined' || typeof kag.current "
	        L"!= 'Object' || typeof kag.current.names == 'undefined') "
	        L"return '';"
	        L"var n = kag.current.names, s = '';"
	        L"var t = ['pageup', 'pagedown', 'top', 'end'];"
	        L"for (var i = 0; i < t.count; i++) {"
	        L"var b = n[t[i]];"
	        L"s += (typeof b == 'Object' && b.enabled) ? '1' : '0'; }"
	        L"var c = n['caption_scene'];"
	        L"s += (typeof c == 'Object' && c.visible) ? '1' : '0';"
	        L"return s; })()",
	        raw, "backlog buttons")) {
		return false;
	}
	if (raw.size() != 5) {
		return false;
	}
	flags = 0;
	for (int i = 0; i < 5; ++i) {
		if (raw[i] == '1') {
			flags |= 1 << i;
		}
	}
	return true;
}

bool ProbeBacklogLineButtons(BacklogLineButtons &buttons) {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof kag == 'undefined' || typeof kag.current "
	        L"!= 'Object') return '';"
	        L"var base = void;"
	        L"for (var i = 0; i < kag.current.children.count; i++) {"
	        L"if (kag.current.children[i].name == 'blockBaseLayer') "
	        L"base = kag.current.children[i]; }"
	        L"if (typeof base != 'Object') return '';"
	        L"var j = '', v = '', w = '';"
	        L"for (var n = 0; n < 5; n++) {"
	        L"var jf = 0, vf = 0, wf = 0;"
	        L"for (var i = 0; i < base.children.count; i++) {"
	        L"var ch = base.children[i];"
	        L"if (ch.name == 'jump' + n && ch.visible) jf = 1;"
	        L"if (ch.name == 'vreplay' + n && ch.visible) vf = 1;"
	        L"if (ch.name == 'vsave' + n && ch.visible) wf = 1; }"
	        L"j += jf; v += vf; w += wf; }"
	        L"return j + v + w; })()",
	        raw, "backlog lines")) {
		return false;
	}
	if (raw.size() != 15) {
		return false;
	}
	buttons = BacklogLineButtons();
	for (int i = 0; i < 5; ++i) {
		if (raw[i] == '1') {
			buttons.jumpMask |= 1 << i;
		}
		if (raw[i + 5] == '1') {
			buttons.voiceMask |= 1 << i;
		}
		if (raw[i + 10] == '1') {
			buttons.saveMask |= 1 << i;
		}
	}
	return true;
}

bool ProbeHistoryCount(int &total) {
	std::string raw;
	if (!QueryString(L"(function(){ if (typeof global.__cm_backlog != 'Object') "
	                 L"return '';"
	                 L"try { return '' + "
	                 L"global.__cm_backlog.playerObject.getHistoryCount(); }"
	                 L"catch (e) { return ''; } })()",
	                 raw, "history count") ||
	    raw.empty()) {
		return false;
	}
	return ParseProbeInt(raw, total, "history count");
}

bool ProbeCurrentLine(int langType, std::string &out) {
	wchar_t probe[1024];
	if (_snwprintf_s(
	        probe, _TRUNCATE,
	        L"(function(){ if (typeof global.__cm_langEntry != 'Object') "
	        L"return '';"
	        L"try { var d = global.__cm_langEntry;"
	        L"var lt = %d < 0 ? kag.languageType : %d;"
	        L"var nm = kag.getLangInfo(d, 'disp', lt);"
	        L"if (nm == '') nm = d.name;"
	        L"return nm + '\\t' + "
	        L"kag.getLangInfo(d, 'text', lt); } catch (e) { return ''; } })()",
	        langType, langType) < 0) {
		Log("[Action] current line probe did not fit\n");
		return false;
	}
	return QueryString(probe, out, "current line") && !out.empty();
}

bool ProbeHistoryWindow(int from, int count, int langType, std::string &out) {
	wchar_t probe[1536];
	if (_snwprintf_s(
	        probe, _TRUNCATE,
	        L"(function(){ if (typeof global.__cm_backlog != 'Object') return '';"
	        // One row per line, fields by tab: a line whose own text carries
	        // either would split the row or shift its fields.
	        L"var flat = function(v) { var t = '' + v; var a = [];"
	        L" a.split('\\t', t); t = a.join(' ');"
	        L" a = []; a.split('\\n', t); t = a.join(' ');"
	        L" a = []; a.split('\\r', t); return a.join(' '); };"
	        L"try { var p = global.__cm_backlog.playerObject;"
	        L"var n = p.getHistoryCount(), s = '';"
	        L"var lt = %d < 0 ? kag.languageType : %d;"
	        L"for (var i = %d; i < %d && i < n; i++) {"
	        L"if (i < 0) continue;"
	        L"var d = p.getHistoryData(i);"
	        L"var nm = kag.getLangInfo(d, 'disp', lt);"
	        L"if (nm == '') nm = d.name;"
	        L"s += (i + 1) + '\\t' + flat(nm) + '\\t' + "
	        L"(d.voice !== void ? '1' : '0') + '\\t' + "
	        L"flat(kag.getLangInfo(d, 'text', lt)) + '\\n'; }"
	        L"return s; } catch (e) { return ''; } })()",
	        langType, langType, from, from + count) < 0) {
		Log("[Action] history window probe did not fit\n");
		return false;
	}
	return QueryString(probe, out, "history window");
}

bool ProbeBacklogBlockCount(int &count) {
	std::string raw;
	// getBlockCount answers for the view, not for the store behind it: the
	// scene list holds one block per scene while the history keeps every line.
	// The count of what is being listed is the only honest "out of".
	if (!QueryString(
	        L"(function(){ if (typeof global.__cm_backlog != 'Object') return '';"
	        L"try { return '' + global.__cm_backlog.getBlockCount(); }"
	        L"catch (e) { return ''; } })()",
	        raw, "backlog block count")) {
		return false;
	}
	int parsed = 0;
	if (!ParseIntToken(raw, parsed) || parsed <= 0) {
		return false;
	}
	count = parsed;
	return true;
}

bool ProbeBacklogWindowRange(int &first, int &count) {
	std::string raw;
	// The list holds the history indices of the rows on screen, one per row, and
	// it follows the scrolling. It is the only place the two numberings meet:
	// everywhere else the rows are known by position and the history by index.
	if (!QueryString(
	        L"(function(){ if (typeof global.__cm_backlog != 'Object') return '';"
	        L"try { var l = global.__cm_backlog._blockButtonInfoList;"
	        L"if (typeof l != 'Object') return '';"
	        L"var n = l.count; if (n <= 0) return '';"
	        L"return '' + l[0] + '	' + n; } catch (e) { return ''; } })()",
	        raw, "backlog window range")) {
		return false;
	}
	size_t const tab = raw.find('	');
	if (tab == std::string::npos) {
		return false;
	}
	int parsedFirst = 0;
	int parsedCount = 0;
	if (!ParseIntToken(raw.substr(0, tab), parsedFirst) ||
	    !ParseIntToken(raw.substr(tab + 1), parsedCount) || parsedCount <= 0) {
		return false;
	}
	first = parsedFirst;
	count = parsedCount;
	return true;
}

bool ProbeSceneWindow(int from, int count, int langType, std::string &out) {
	wchar_t probe[1024];
	if (_snwprintf_s(
	        probe, _TRUNCATE,
	        L"(function(){ if (typeof global.__cm_backlog != 'Object') return '';"
	        L"try { var b = global.__cm_backlog;"
	        L"var n = b.getBlockCount(), s = '';"
	        L"var lt = %d < 0 ? kag.languageType : %d;"
	        L"for (var i = %d; i < %d && i < n; i++) {"
	        L"if (i < 0) continue;"
	        // One block per scene, and the block is the record the scene opens
	        // with -- which is why it carries a voice of its own. Walking the
	        // history instead would list lines, and every line of one scene
	        // would print that scene's name.
	        L"var d = b.getBlockInfo(i);"
	        // One entry per language, and the game picks by the same index it
	        // uses everywhere else. A scene with no title at all stays empty
	        // rather than printing the array.
	        L"var t = d.title;"
	        L"if (typeof t == 'Object' && t) t = t[+lt];"
	        L"if (t === void) t = '';"
	        L"s += (i + 1) + '\\t' + ('' + d.scene) + '\\t' + ('' + t) + '\\n'; }"
	        L"return s; } catch (e) { return ''; } })()",
	        langType, langType, from, from + count) < 0) {
		Log("[Action] scene window probe did not fit\n");
		return false;
	}
	return QueryString(probe, out, "scene window");
}

bool DispatchBacklogLineButton(char const *verb, int index, char const *tag) {
	// The button's own expression is Current.cmd("<verb>/<index>"); calling it
	// directly is the same dispatch, not a shortcut around it. The preview
	// verbs only redraw, so they run in place and leave the pending flag free
	// for the press that follows them.
	if (strcmp(verb, "showJumpView") == 0 || strcmp(verb, "hideJumpView") == 0) {
		return AskFormatted(
		    L"(function(){ if (typeof Current != 'Object') return 0;"
		    L"Current.cmd('%hs/%d'); return 1; })()",
		    tag, verb, index);
	}
	return AskDeferred(L"if (typeof Current != 'Object') return 0;",
	                   Format(L"if (typeof Current == 'Object') "
	                          L"Current.cmd('%hs/%d');",
	                          verb, index),
	                   tag);
}

bool ProbeGallery(GalleryState &state) {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof Current != 'Object' || typeof kag.current "
	        L"!= 'Object') return '';"
	        L"try { var s = '' + Current.page + '|';"
	        // Page buttons past the last one exist but are invisible.
	        L"var pg = 0;"
	        L"for (var i = 0; i < 16; i++) {"
	        L"var b = kag.current.names['page' + i];"
	        L"if (typeof b == 'Object' && b.visible) pg = i + 1; }"
	        L"s += pg + '|';"
	        // The last page uses fewer than nine cells; the rest are invisible.
	        L"var sq = 0;"
	        L"for (var i = 0; i < 9; i++) {"
	        L"var c = kag.current.names['item' + i];"
	        L"if (typeof c == 'Object' && c.visible) sq = i + 1; }"
	        L"s += sq + '|';"
	        // The list beside the grid is its own set of controls; how many it
	        // offers is simply how many are on screen.
	        L"var t = 0;"
	        L"for (var i = 0; i < 32; i++) {"
	        L"var b = kag.current.names['sitem' + i];"
	        L"if (typeof b == 'Object' && b.visible) t = i + 1; }"
	        L"s += t + '|';"
	        // Where the visible rows sit in the whole list. The numbers the
	        // actions use are absolute, and this is what turns one back into the
	        // button that carries it.
	        L"var off = 0;"
	        L"try { var sm = global.SoundModeInstance;"
	        L"if (typeof sm == 'Object' && isvalid sm &&"
	        L" typeof sm.scutil == 'Object' && isvalid sm.scutil)"
	        L" off = sm.scutil.offset; } catch (e) { }"
	        L"s += off + '|';"
	        L"var arrows = ['up', 'down', 'pageup', 'pagedown'];"
	        L"for (var i = 0; i < arrows.count; i++) {"
	        L"var b = kag.current.names[arrows[i]];"
	        L"s += (typeof b == 'Object' && b.enabled) ? '1' : '0'; }"
	        // Absent controller means nothing has been opened yet, which is the
	        // same answer as "closed".
	        // Whether something covers the grid, asked two ways because neither
	        // covers both kinds of entry. A video replaces the screen outright,
	        // taking the grid's own controls with it. A picture leaves them in
	        // place and shows on its own layer, so that layer's visibility is
	        // what says it is up.
	        //
	        // isQuitViewLoop must NOT be used for this: it turns true on the
	        // last view while that view is still on screen, so the grid actions
	        // would register over a picture the player can still see. It is
	        // still the right source for how many views remain.
	        L"var open = kag.current.names['back'] === void ? 1 : 0, left = 0;"
	        L"try { if (typeof global.__cm_gallery == 'Object') {"
	        L"var g = global.__cm_gallery;"
	        L"if (typeof g.cglayer == 'Object' && g.cglayer.visible) open = 1;"
	        L"if (!g.isQuitViewLoop) {"
	        L"left = g.subItems.count - g.subItemPos; } } } catch (e) { }"
	        L"return s + '|' + open + '|' + left; }"
	        L"catch (e) { return ''; } })()",
	        raw, "gallery")) {
		return false;
	}

	// "<page>|<pages>|<squares>|<tracks>|<offset>|<four arrow flags>|<open>|
	// <variants left>"
	int values[5] = {0, 0, 0, 0, 0};
	size_t cursor = 0;
	for (int field = 0; field < 5; ++field) {
		size_t const bar = raw.find('|', cursor);
		if (bar == std::string::npos) {
			return false;
		}
		if (!ParseProbeInt(raw.substr(cursor, bar - cursor), values[field],
		                   "gallery")) {
			return false;
		}
		cursor = bar + 1;
	}
	if (raw.size() < cursor + 4) {
		return false;
	}
	state.page = values[0];
	state.pageCount = values[1];
	state.squareCount = values[2];
	state.trackCount = values[3];
	state.trackOffset = values[4];
	state.scrollMask = 0;
	for (int i = 0; i < 4; ++i) {
		if (raw[cursor + i] == '1') {
			state.scrollMask |= 1 << i;
		}
	}
	cursor += 5;
	if (cursor >= raw.size()) {
		return false;
	}
	state.viewOpen = raw[cursor] == '1';
	size_t const last = raw.find('|', cursor);
	// No trailing field at all is a legitimate "none left"; a field that is
	// there but unreadable is not, and must not be reported as zero.
	state.variantsLeft = 0;
	if (last != std::string::npos &&
	    !ParseProbeInt(raw.substr(last + 1), state.variantsLeft, "gallery")) {
		return false;
	}
	return true;
}

bool ProbeCgScroll(CgScrollState &state, char const *tag) {
	std::string raw;
	if (!QueryString(L"(function(){ var g = global.__cm_gallery;"
	                 L"if (typeof g != 'Object' || !g) return '';"
	                 L"var l = g.cglayer;"
	                 L"if (typeof l != 'Object' || !isvalid l) return '';"
	                 L"try { return l.canScroll + '|' + l.scrollLeft + '|' + "
	                 L"l.scrollMaxLeft"
	                 L" + '|' + l.scrollTop + '|' + l.scrollMaxTop; }"
	                 L"catch (e) { return ''; } })()",
	                 raw, tag) ||
	    raw.empty()) {
		return false;
	}

	int values[5] = {0, 0, 0, 0, 0};
	size_t cursor = 0;
	for (int field = 0; field < 5; ++field) {
		size_t const bar = raw.find('|', cursor);
		std::string const piece = bar == std::string::npos
		                              ? raw.substr(cursor)
		                              : raw.substr(cursor, bar - cursor);
		if (!ParseProbeInt(piece, values[field], tag)) {
			return false;
		}
		if (bar == std::string::npos) {
			if (field != 4) {
				return false;
			}
			break;
		}
		cursor = bar + 1;
	}
	state.canScroll = values[0];
	state.left = values[1];
	state.maxLeft = values[2];
	state.top = values[3];
	state.maxTop = values[4];
	return true;
}

bool DispatchCgScroll(int horizontal, int vertical, char const *tag) {
	return AskFormatted(
	    L"(function(){ var g = global.__cm_gallery;"
	    L"if (typeof g != 'Object' || !g) return 0;"
	    L"var l = g.cglayer;"
	    L"if (typeof l != 'Object' || !isvalid l) return 0;"
	    L"if (typeof l.scrollByKey != 'Object') return 0;"
	    L"l.scrollByKey(%d, %d); return 1; })()",
	    tag, horizontal, vertical);
}

bool ProbeMusicVolume(int &volume, char const *tag) {
	std::string raw;
	if (!QueryString(L"(function(){ try { var c = kag.current.names['volume'];"
	                 L"if (typeof c != 'Object' || !isvalid c) return '';"
	                 L"return '' + c.position; } catch (e) { return ''; } })()",
	                 raw, tag) ||
	    raw.empty()) {
		return false;
	}
	return ParseProbeInt(raw, volume, tag);
}

bool DispatchMusicVolume(int volume, char const *tag) {
	return AskFormatted(
	    L"(function(){ try {"
	    L"var c = kag.current.names['volume'];"
	    L"if (typeof c != 'Object' || !isvalid c) return 0;"
	    L"sf.musicvolume = %d;"
	    L"if (kag.bgmenable) kag.bgm.restoreSystemState("
	    L"%%[ bgm: %%[ globalVolume: %d * 1000 ] ]);"
	    L"c.position = %d;"
	    L"return 1; } catch (e) { return 0; } })()",
	    tag, volume, volume, volume);
}

bool ProbeMusicList(std::string &rows, char const *tag) {
	// The buttons on screen are the visible rows, and the list slides under them:
	// button i shows track i + offset, which is the same sum the screen's own
	// select() applies before it plays anything.
	//
	// Rows are numbered by that sum rather than by the button, so a number names
	// one track whatever the list has been scrolled to. The offset rides in the
	// header beside the total, because turning a number back into a button needs
	// it.
	return QueryString(
	    L"(function(){ try { var s = global.SoundModeInstance;"
	    L"if (typeof s != 'Object' || !isvalid s) return '';"
	    L"var u = s.scutil;"
	    L"if (typeof u != 'Object' || !isvalid u) return '';"
	    L"var off = u.offset, out = '';"
	    L"for (var i = 0; i < 32; i++) {"
	    L"var b = kag.current.names['sitem' + i];"
	    L"if (typeof b != 'Object' || !b.visible) continue;"
	    L"var idx = off + i;"
	    L"var t = s.getTitle(idx);"
	    L"if (t === void) continue;"
	    L"out += '\\n' + (idx + 1) + '\\t' + s.storages[idx] + '\\t' + t"
	    L" + '\\t' + (s.isPlaying(idx) ? '1' : '0'); }"
	    L"if (out == '') return '';"
	    L"return '' + s.items.count + '\\t' + off + out;"
	    L"} catch (e) { return ''; } })()",
	    rows, tag);
}

bool ProbeMusicPlaying(std::string &storage,
                       std::string &title,
                       char const *tag) {
	std::string raw;
	if (!QueryString(L"(function(){ try { var s = global.SoundModeInstance;"
	                 L"if (typeof s != 'Object' || !isvalid s) return '';"
	                 L"var p = s.playing;"
	                 L"if (p === void || p == '') return '';"
	                 L"var i = s.findStorageNumber(p);"
	                 L"var t = i >= 0 ? s.getTitle(i) : void;"
	                 L"return '' + p + '|' + (t === void ? '' : t);"
	                 L"} catch (e) { return ''; } })()",
	                 raw, tag) ||
	    raw.empty()) {
		return false;
	}
	std::string::size_type const bar = raw.find('|');
	if (bar == std::string::npos) {
		return false;
	}
	storage = raw.substr(0, bar);
	title = raw.substr(bar + 1);
	return !storage.empty();
}

bool ProbeMusicScroll(int &position, int &limit, char const *tag) {
	std::string raw;
	if (!QueryString(L"(function(){ try { var s = global.SoundModeInstance;"
	                 L"if (typeof s != 'Object' || !isvalid s) return '';"
	                 L"var u = s.scutil;"
	                 L"if (typeof u != 'Object' || !isvalid u) return '';"
	                 L"if (!u.canScroll) return '';"
	                 L"return '' + u.scroll + '|' + u.scrollMax;"
	                 L"} catch (e) { return ''; } })()",
	                 raw, tag) ||
	    raw.empty()) {
		return false;
	}
	std::string::size_type const bar = raw.find('|');
	if (bar == std::string::npos) {
		return false;
	}
	return ParseProbeInt(raw.substr(0, bar), position, tag) &&
	       ParseProbeInt(raw.substr(bar + 1), limit, tag);
}

bool DispatchMusicScroll(int position, char const *tag) {
	// The screen's own handler, called the way the bar calls it when it is
	// released: that path rounds to a whole row and carries the list along.
	// Writing the knob alone would move the bar and leave the list where it was.
	return AskFormatted(
	    L"(function(){ try { var s = global.SoundModeInstance;"
	    L"if (typeof s != 'Object' || !isvalid s) return 0;"
	    L"if (typeof s.onSliderChanged != 'Object') return 0;"
	    L"s.onSliderChanged(%d); return 1; } catch (e) { return 0; } })()",
	    tag, position);
}

bool DispatchGalleryItem(char const *verb, int index, char const *tag) {
	return AskDeferred(L"if (typeof Current != 'Object') return 0;",
	                   Format(L"if (typeof Current == 'Object') "
	                          L"Current.cmd('%hs/%d');",
	                          verb, index),
	                   tag);
}

bool DispatchGalleryNextView(char const *tag) {
	// The scenario waits for a click at [s] with the picture layer holding
	// focus, so the click has to be delivered into that layer -- the window's
	// own onPrimaryClick never reaches it. Four arguments: the two-argument form
	// is rejected outright.
	return AskDeferred(
	    L"if (typeof global.__cm_gallery != 'Object') return 0;"
	    L" try { if (typeof global.__cm_gallery.cglayer != 'Object') return 0; }"
	    L" catch (e) { return 0; }",
	    L"var l = global.__cm_gallery.cglayer;"
	    L" if (typeof l != 'Object') return;"
	    L" l.onMouseUp(l.width \\ 2, l.height \\ 2, mbLeft, 0);",
	    tag);
}

bool DispatchGalleryExit(char const *tag) {
	return AskDeferred(
	    L"if (typeof kag == 'undefined' || typeof "
	    L"kag.onPrimaryRightClick != 'Object') return 0;",
	    L"kag.onPrimaryRightClick();", tag);
}

bool HoverGalleryItem(char const *prefix, int index, char const *tag) {
	// The control carries its hover as an expression rather than a command, so
	// this evaluates that expression instead of calling a named entry point.
	return AskFormatted(
	    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
	    L"'Object') return 0;"
	    L"var b = kag.current.names['%hs%d'];"
	    L"if (typeof b != 'Object' || typeof b.onenter != 'String' || "
	    L"b.onenter == '') return 0;"
	    L"Scripts.eval(b.onenter); return 1; })()",
	    tag, prefix, index);
}

bool DispatchExitRequest(char const *tag) {
	return AskDeferred(
	    L"if (typeof SystemAction != 'Object' || typeof "
	    L"SystemAction.exit != 'Object') return 0;",
	    L"SystemAction.exit();", tag);
}

int ProbeTabCount() {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof kag == 'undefined' || typeof "
	        L"kag.current != 'Object' || typeof kag.current.names == "
	        L"'undefined') return '0';"
	        // The slot screens carry page buttons of their own, over a
	        // list rather than over tabs. Counting those as settings
	        // tabs offered a sixteen-tab settings screen on top of the
	        // save list, so a slot grid means "no tabs here" outright.
	        L"if (typeof kag.current.names['item00'] == 'Object') return "
	        L"'0';"
	        // The tab that is open has no button -- you do not press
	        // the one you are on -- so the controls always come one
	        // short of the real count, whichever tab that is. Counting
	        // what is there and adding the current one back gives the
	        // total; the missing index is exactly the open tab.
	        L"var n = 0;"
	        L"for (var i = 0; i < 16; i++) {"
	        L"var b = kag.current.names['page' + i];"
	        L"if (typeof b == 'Object') n++; }"
	        L"return '' + (n > 0 ? n + 1 : 0); })()",
	        raw, "tab count")) {
		return 0;
	}
	int count = 0;
	return ParseProbeInt(raw, count, "tab count") ? count : 0;
}

bool ProbeDialogVisible(char const *tag) {
	return Ask(
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.currentDialog != 'Object') return 0;"
	    L"return kag.currentDialog.visible ? 1 : 0; })()",
	    tag);
}

bool ProbeDialogTextPart(std::string &part) {
	std::string raw;
	if (!QueryString(L"(function(){ try { return '' + "
	                 L"MyYesNoDialog.currentTextPart; } catch(e) { return ''; "
	                 L"} })()",
	                 raw, "dialog text part")) {
		return false;
	}
	part = raw;
	return !part.empty();
}

bool ProbeDialogLinkReady(bool confirm, char const *tag) {
	// 'Object', not 'undefined': kag.currentDialog holds void when no dialog is
	// up, and typeof reports "void" for that, so an "undefined" test would pass
	// and the property access below would throw.
	return AskFormatted(
	    L"(function(){ if (typeof kag == 'undefined' || typeof kag.currentDialog "
	    L"!= 'Object' || !kag.currentDialog.visible) return 0;"
	    L"var link = kag.currentDialog.names['%hs'];"
	    L"return typeof link == 'Object' ? 1 : 0; })()",
	    tag, confirm ? "yes" : "no");
}

bool DispatchDialogLink(bool confirm, char const *tag) {
	char const *const button = confirm ? "yes" : "no";
	// The one press that had no cursor arriving before it. Every screen got the
	// hover; the dialog was answered by reaching straight for processLink, so
	// the answer appeared with nothing lighting up -- and the game's own default
	// focus sits on No, which made every answer look like No.
	// The handler name appears twice in the expression: once to test that the
	// button has it, once to call it.
	if (AskFormatted(kDialogLinkHover, tag, button, "onMouseEnter",
	                 "onMouseEnter")) {
		Sleep(kHoverPreviewMs);
	}
	return AskDeferred(
	    L"",
	    Format(L"var d = kag.currentDialog;"
	           L" if (typeof d != 'Object' || !d) return;"
	           L" var b = d.names['%hs'];"
	           L" if (typeof b == 'Object') d.processLink(b.linkNum);",
	           button),
	    tag);
}
