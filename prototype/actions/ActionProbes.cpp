#include "ActionProbes.h"

#include <windows.h>

#include <string>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "Logging.h"
#include "TJSExecutor.h"
#include "TJSQuery.h"
#include "UtfConversion.h"

namespace {

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
	wchar_t probe[768];
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

wchar_t const *const kScreenButtonReady =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.current != "
    L"'Object' || typeof kag.current.names == 'undefined') return 0;"
    L"var button = kag.current.names['%hs'];"
    L"if (typeof button != 'Object' || !isvalid button || !button.visible || "
    L"!button.enabled) return 0;"
    L"if (typeof button.controlOwner == 'undefined' || typeof "
    L"button.controlOwner.onButtonClick != 'Object') return 0;"
    L"return 1; })()";

wchar_t const *const kScreenButtonDispatch =
    L"(function(){ var button = kag.current.names['%hs'];"
    L"button.controlOwner.onButtonClick(button.linkNum); return 1; })()";

wchar_t const *const kQuickMenuButtonReady =
    L"(function(){ if (typeof kag == 'undefined' || typeof kag.QuickMenu != "
    L"'Object') return 0;"
    L"var button = kag.QuickMenu.names['%hs'];"
    L"if (typeof button != 'Object' || !isvalid button || !button.visible || "
    L"!button.enabled) return 0;"
    L"if (typeof button.controlOwner == 'undefined' || typeof "
    L"button.controlOwner.onButtonClick != 'Object') return 0;"
    L"return 1; })()";

wchar_t const *const kQuickMenuButtonDispatch =
    L"(function(){ var button = kag.QuickMenu.names['%hs'];"
    L"button.controlOwner.onButtonClick(button.linkNum); return 1; })()";

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
	// Voice tab inside the save/load screen, confirmed live 2026-08-20, and is a
	// different screen from "voicemode" (the voice settings page).
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
	tTJSVariant result;
	bool const executed = ExecuteTJSSync(
	    L"(function(){ var s = typeof SystemHook != 'undefined' ? "
	    L"SystemHook.currentState : '';"
	    // Test for 'Object', not for 'undefined': kag.currentDialog exists as a
	    // member and simply holds void when no dialog is up, and typeof reports
	    // "void" for that -- an "undefined" test passes and the following
	    // isvalid/property access then throws on the void value.
	    L"var d = 0, c = 0, p = 0, h = 0, a = 0, s2 = 0, v = 0, b = 0, n = 0;"
	    L"if (typeof kag != 'undefined') {"
	    L"if (typeof kag.currentDialog == 'Object' && kag.currentDialog.visible) "
	    L"d = 1;"
	    L"if (kag.selectShowing) c = 1;"
	    L"if (typeof kag.QuickMenu == 'Object' && kag.QuickMenu._dropdown) p = 1;"
	    L"if (kag.messageLayerHiding) h = 1;"
	    L"if (kag.autoMode) a = 1;"
	    L"if (kag.skipMode) s2 = 1;"
	    L"if (typeof kag.QuickMenu == 'Object') {"
	    L"var q = kag.QuickMenu.names;"
	    L"if (typeof q['vreplay'] == 'Object' && q['vreplay'].enabled) v = 1;"
	    L"if (typeof q['xprev'] == 'Object' && q['xprev'].enabled) b = 1;"
	    L"if (typeof q['next'] == 'Object' && q['next'].enabled) n = 1;"
	    L"}"
	    L"}"
	    L"return s + '|' + d + '|' + c + '|' + p + '|' + h + '|' + a + '|' + s2 + "
	    L"'|' + v + '|' + b + '|' + n; })()",
	    &result, 5000, TJSExecMode::Expression);
	if (!executed) {
		Log("[Action] Refresh state probe failed\n");
		return false;
	}

	ttstr value(result);
	std::string probe;
	if (!Utf16ToUtf8(value.c_str(), value.GetLen(), probe)) {
		Log("[Action] Refresh state conversion failed\n");
		return false;
	}

	// "<state>|<d>|<c>|<p>|<h>|<a>|<s>": the state can be empty but never
	// contains a bar, so flags are read off the tail by offset from the first.
	size_t const separator = probe.find('|');
	if (separator == std::string::npos || probe.size() != separator + 18) {
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
	out.canReplayVoice = probe[separator + 13] == '1';
	out.canGoPrevious = probe[separator + 15] == '1';
	out.canGoNext = probe[separator + 17] == '1';
	return true;
}

bool ProbeScreenButtonReady(char const *button, char const *tag) {
	return AskFormatted(kScreenButtonReady, tag, button);
}

bool DispatchScreenButton(char const *button, char const *tag) {
	return AskFormatted(kScreenButtonDispatch, tag, button);
}

bool ProbeCanContinue(char const *tag) {
	return Ask(L"typeof SystemAction != 'undefined' && SystemAction.canContinue "
	           L"? 1 : 0",
	           tag);
}

bool ProbeClickWaiting(char const *tag) {
	return Ask(L"(function(){ if (typeof kag == 'undefined' || typeof "
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
	return Ask(L"(function(){ if (typeof kag == 'undefined' || typeof "
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
	return AskFormatted(L"(function(){ var layer = kag.selectLayer;"
	                    L"var button = layer.selects[%d];"
	                    L"layer.onButtonClick(button); return 1; })()",
	                    tag, index);
}

bool ProbeQuickMenuCanDropDown(char const *tag) {
	return Ask(L"(function(){ if (typeof kag == 'undefined' || typeof "
	           L"kag.QuickMenu != 'Object') return 0;"
	           L"if (typeof kag.QuickMenu.updown != 'Object') return 0;"
	           L"if (!kag.QuickMenu.canDropDown) return 0;"
	           L"return 1; })()",
	           tag);
}

bool ProbeQuickMenuHasUpdown(char const *tag) {
	return Ask(L"(function(){ if (typeof kag == 'undefined' || typeof "
	           L"kag.QuickMenu != 'Object') return 0;"
	           L"if (typeof kag.QuickMenu.updown != 'Object') return 0;"
	           L"return 1; })()",
	           tag);
}

bool ProbeQuickMenuIsOut(char const *tag) {
	return Ask(L"(function(){ if (typeof kag == 'undefined' || typeof "
	           L"kag.QuickMenu != 'Object') return 0;"
	           L"return kag.QuickMenu._dropdown ? 1 : 0; })()",
	           tag);
}

bool DispatchQuickMenuOpen(char const *tag) {
	return Ask(L"(function(){ kag.QuickMenu.updown(1); return "
	           L"kag.QuickMenu._dropdown ? 1 : 0; })()",
	           tag);
}

bool DispatchQuickMenuClose(char const *tag) {
	return Ask(L"(function(){ kag.QuickMenu.updown(0); return "
	           L"kag.QuickMenu._dropdown ? 0 : 1; })()",
	           tag);
}

bool ProbeQuickMenuButtonReady(char const *button, char const *tag) {
	return AskFormatted(kQuickMenuButtonReady, tag, button);
}

bool DispatchQuickMenuButton(char const *button, char const *tag) {
	return AskFormatted(kQuickMenuButtonDispatch, tag, button);
}

bool DispatchShowTextWindow(char const *tag) {
	// showMessageLayerByUser() is what the engine reaches when the player clicks
	// or presses Escape while the window is away; it clears messageLayerHiding
	// and re-triggers the conductor. Confirmed live 2026-08-20: 1 -> 0. There is
	// no control to route through here -- the bar's own hide button reports
	// enabled = 0 once the window is gone.
	return Ask(L"(function(){ if (typeof kag == 'undefined' || typeof "
	           L"kag.showMessageLayerByUser != 'Object') return 0;"
	           L"kag.showMessageLayerByUser();"
	           L"return kag.messageLayerHiding ? 0 : 1; })()",
	           tag);
}

bool ProbeBookmarkExists(int number, char const *tag) {
	return AskFormatted(L"(function(){ if (typeof kag == 'undefined' || typeof "
	                    L"kag.isExistentBookMark != 'Object') return 0;"
	                    L"if (%d < 0 || %d >= kag.numBookMarks) return 0;"
	                    L"return kag.isExistentBookMark(%d) ? 1 : 0; })()",
	                    tag, number, number, number);
}

bool ProbeLoadAccepted(char const *tag) {
	return Ask(L"(function(){ if (typeof kag == 'undefined') return 0;"
	           L"if (typeof kag.currentDialog == 'Object' && "
	           L"kag.currentDialog.visible) return 1;"
	           L"if (typeof SystemHook != 'undefined' && "
	           L"SystemHook.currentState == 'game') return 1;"
	           L"return 0; })()",
	           tag);
}

bool ProbeSaveWindow(int &offset, int &page) {
	// Every access is wrapped: the instance is invalidated the moment the screen
	// closes, and touching it then throws rather than returning void.
	std::string raw;
	if (!QueryString(L"(function(){ if (typeof global.__cm_saveStore != 'Object') "
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
	offset = atoi(raw.substr(0, separator).c_str());
	page = atoi(raw.substr(separator + 1).c_str());
	return true;
}

bool ProbeFreeSlots(int limit, std::string &out, int &total) {
	// Starts at 10: 0..9 are the quick saves, which sit on their own tab.
	std::string raw;
	wchar_t probe[768];
	if (_snwprintf_s(probe, _TRUNCATE,
	                 L"(function(){ if (typeof kag == 'undefined') return '0';"
	                 L"var found = 0, listed = 0, s = '';"
	                 L"for (var i = 10; i < kag.numBookMarks; i++) {"
	                 L"if (kag.isExistentBookMark(i)) continue;"
	                 L"found++;"
	                 L"if (listed < %d) { listed++; s += ' ' + (i + 1); } }"
	                 L"return found + s; })()",
	                 limit) < 0) {
		Log("[Action] free slot probe did not fit\n");
		return false;
	}
	if (!QueryString(probe, raw, "free slots")) {
		return false;
	}
	size_t const space = raw.find(' ');
	total = atoi(raw.substr(0, space).c_str());
	out = space == std::string::npos ? std::string() : raw.substr(space + 1);
	return true;
}

bool ProbeVoiceExists(int index, char const *tag) {
	return AskFormatted(
	    L"(function(){ if (typeof global.__cm_saveStore != 'Object') return 0;"
	    L"try { return global.__cm_saveStore.isExistData(%d) ? 1 : 0; }"
	    L"catch (e) { return 0; } })()",
	    tag, index);
}

bool ProbeVoiceList(int limit, std::string &out, int &total) {
	wchar_t probe[1024];
	if (_snwprintf_s(
	        probe, _TRUNCATE,
	        L"(function(){ if (typeof global.__cm_saveStore != 'Object') "
	        L"return '0';"
	        L"var st = global.__cm_saveStore;"
	        L"var found = 0, listed = 0, s = '';"
	        L"try { for (var i = 0; i < st.count; i++) {"
	        L"if (!st.isExistData(i)) continue;"
	        L"found++;"
	        L"if (listed < %d) { listed++; var d = st.getItemInfo(i);"
	        L"s += '\n' + (i + 1) + '\t' + d.name + '\t' + d.title; } }"
	        L"} catch (e) { }"
	        L"return found + s; })()",
	        limit) < 0) {
		Log("[Action] voice list probe did not fit\n");
		return false;
	}
	std::string raw;
	if (!QueryString(probe, raw, "voice list")) {
		return false;
	}
	size_t const firstLine = raw.find('\n');
	total = atoi(raw.substr(0, firstLine).c_str());
	out = firstLine == std::string::npos ? std::string()
	                                     : raw.substr(firstLine + 1);
	return true;
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

bool ProbeBookmarkList(int limit, std::string &out, int &total) {
	wchar_t probe[1024];
	int const written = _snwprintf_s(
	    probe, _TRUNCATE,
	    L"(function(){ if (typeof kag == 'undefined' || typeof "
	    L"kag.isExistentBookMark != 'Object') return '0';"
	    L"var found = 0, listed = 0, s = '';"
	    L"for (var i = 0; i < kag.numBookMarks; i++) {"
	    L"if (!kag.isExistentBookMark(i)) continue;"
	    L"found++;"
	    L"if (listed < %d) { listed++;"
	    L"s += '\n' + (i + 1) + '\t' + kag.getBookMarkPageName(i) + '\t' + "
	    L"kag.getBookMarkDate(i); } }"
	    L"return found + s; })()",
	    limit);
	if (written < 0) {
		Log("[Action] bookmark list probe did not fit\n");
		return false;
	}

	std::string raw;
	if (!QueryString(probe, raw, "bookmark list")) {
		return false;
	}
	size_t const firstLine = raw.find('\n');
	total = atoi(raw.substr(0, firstLine).c_str());
	out = firstLine == std::string::npos ? std::string() : raw.substr(firstLine + 1);
	return true;
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

bool ProbeBacklogLineButtons(int &jumpMask, int &voiceMask) {
	std::string raw;
	if (!QueryString(
	        L"(function(){ if (typeof kag == 'undefined' || typeof kag.current "
	        L"!= 'Object') return '';"
	        L"var base = void;"
	        L"for (var i = 0; i < kag.current.children.count; i++) {"
	        L"if (kag.current.children[i].name == 'blockBaseLayer') "
	        L"base = kag.current.children[i]; }"
	        L"if (typeof base != 'Object') return '';"
	        L"var j = '', v = '';"
	        L"for (var n = 0; n < 5; n++) {"
	        L"var jf = 0, vf = 0;"
	        L"for (var i = 0; i < base.children.count; i++) {"
	        L"var ch = base.children[i];"
	        L"if (ch.name == 'jump' + n && ch.visible) jf = 1;"
	        L"if (ch.name == 'vreplay' + n && ch.visible) vf = 1; }"
	        L"j += jf; v += vf; }"
	        L"return j + v; })()",
	        raw, "backlog lines")) {
		return false;
	}
	if (raw.size() != 10) {
		return false;
	}
	jumpMask = 0;
	voiceMask = 0;
	for (int i = 0; i < 5; ++i) {
		if (raw[i] == '1') {
			jumpMask |= 1 << i;
		}
		if (raw[i + 5] == '1') {
			voiceMask |= 1 << i;
		}
	}
	return true;
}

bool DispatchBacklogLineButton(char const *verb, int index, char const *tag) {
	// The button's own expression is Current.cmd("<verb>/<index>"); calling it
	// directly is the same dispatch, not a shortcut around it.
	return AskFormatted(L"(function(){ if (typeof Current != 'Object') return 0;"
	                    L"Current.cmd('%hs/%d'); return 1; })()",
	                    tag, verb, index);
}

bool ProbeDialogVisible(char const *tag) {
	return Ask(L"(function(){ if (typeof kag == 'undefined' || typeof "
	           L"kag.currentDialog != 'Object') return 0;"
	           L"return kag.currentDialog.visible ? 1 : 0; })()",
	           tag);
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
	return AskFormatted(L"(function(){ var d = kag.currentDialog;"
	                    L"d.processLink(d.names['%hs'].linkNum); return 1; })()",
	                    tag, confirm ? "yes" : "no");
}
