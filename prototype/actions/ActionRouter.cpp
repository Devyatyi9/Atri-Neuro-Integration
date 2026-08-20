#include "ActionRouter.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "Logging.h"
#include "NeuroClient.h"
#include "TJSExecutor.h"
#include "TJSQuery.h"
#include "UtfConversion.h"

namespace {

std::atomic<bool> g_ContinueActionInFlight(false);
std::atomic<ULONGLONG> g_ContinueActionStartedAt(0);
std::mutex g_ActionIdsMutex;
std::deque<std::string> g_ActionIds;
std::unordered_set<std::string> g_ActionIdSet;
constexpr size_t kMaxRememberedActionIds = 256;

std::atomic<int> g_SyncedCapabilityState(-1);
// Whether the "still starting up" notice reached the server. It cannot simply
// be sent once on entering the loading state, because that happens before the
// SDK connects.
std::atomic<bool> g_TitleLoadingNoticeSent(false);
constexpr int kCapabilityStateUnknown = -1;
constexpr int kCapabilityStateTitle = 0;
constexpr int kCapabilityStateGame = 1;
constexpr int kCapabilityStateMenu = 2;
// Startup and the title screen are distinct capability states. The plugin
// initialises long before the title exists -- splashes run for roughly 24s --
// so registering continue_game at init advertised an action that was certain to
// fail for that whole window, and an autonomous engine does call it (observed
// in Gary: randy dispatched continue_game on its own). This state carries no
// actions and explains the wait through context instead.
constexpr int kCapabilityStateTitleLoading = 3;

char const *kContinueGameSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
char const *kAdvanceTextSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
char const *kSelectChoiceSchema =
    "{\"type\":\"object\",\"properties\":{"
    "\"index\":{\"type\":\"integer\",\"minimum\":0}},"
    "\"required\":[\"index\"],"
    "\"additionalProperties\":false}";
char const *kGoBackSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
char const *kOpenMenuSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
char const *kOpenQuickMenuSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";

bool ParseJsonInteger(std::string const &json, char const *key, int &out) {
	size_t const keyPos = json.find(key);
	if (keyPos == std::string::npos) {
		return false;
	}
	size_t colon = json.find(':', keyPos + strlen(key));
	if (colon == std::string::npos) {
		return false;
	}
	size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
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

bool ClaimActionId(std::string const &id) {
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	if (!g_ActionIdSet.insert(id).second) {
		return false;
	}
	g_ActionIds.push_back(id);
	if (g_ActionIds.size() > kMaxRememberedActionIds) {
		g_ActionIdSet.erase(g_ActionIds.front());
		g_ActionIds.pop_front();
	}
	return true;
}

bool IsEmptyJsonObject(std::string const &data) {
	size_t const first = data.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		// No data field, or an empty/all-whitespace string: the real Neuro
		// client omits `data` entirely for actions with an empty-properties
		// schema, which reaches the plugin as "".
		return true;
	}
	if (data[first] != '{') {
		return false;
	}
	size_t const last = data.find_last_not_of(" \t\r\n");
	return last != std::string::npos && last == first + 1 && data[last] == '}';
}

void HandleContinueAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "continue_game does not accept arguments.");
		return;
	}
	if (g_ContinueActionInFlight.exchange(true, std::memory_order_acq_rel)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Continue is already being started.");
		return;
	}
	g_ContinueActionStartedAt.store(GetTickCount64(), std::memory_order_release);

	bool ok = true;
	bool ready = false;

	// 1. Current state must be title.
	std::string state;
	if (ok && !QueryString(
	              L"typeof SystemHook != 'undefined' ? SystemHook.currentState "
	              L": ''",
	              state, "continue state")) {
		ok = false;
	} else if (state != "title") {
		ok = false;
	}

	// 2. A suspend-save with tag "continue" must exist.
	if (ok && !QueryBool(L"typeof SystemAction != 'undefined' && "
	                     L"SystemAction.canContinue ? 1 : 0",
	                     ready, "continue canContinue")) {
		ok = false;
	} else if (ok && !ready) {
		ok = false;
	}

	// 3. The Continue button must be visible, enabled, and dispatchable.
	if (ok &&
	    !QueryBool(L"(function(){ if (typeof kag == 'undefined' || typeof "
	               L"kag.current == 'undefined' || typeof kag.current.names == "
	               L"'undefined') return 0;"
	               L"var button = kag.current.names['cont'];"
	               L"if (typeof button == 'undefined' || !isvalid button || "
	               L"!button.visible || !button.enabled) return 0;"
	               L"if (typeof button.controlOwner == 'undefined' || typeof "
	               L"button.controlOwner.onButtonClick != 'Object') return 0;"
	               L"return 1; })()",
	               ready, "continue button")) {
		ok = false;
	} else if (ok && !ready) {
		ok = false;
	}

	// 4. Dispatch the button click.
	bool dispatched = false;
	if (ok) {
		dispatched = QueryBool(
		    L"(function(){ var button = kag.current.names['cont'];"
		    L"button.controlOwner.onButtonClick(button.linkNum); return 1; })()",
		    dispatched, "continue dispatch");
	}

	if (!ok || !dispatched) {
		g_ContinueActionInFlight.store(false, std::memory_order_release);
		NeuroSendActionResult(id.c_str(), false, "Continue is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Continue started.");
}

void HandleAdvanceTextAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "advance_text does not accept arguments.");
		return;
	}

	bool ok = true;
	bool ready = false;

	// 1. Current state must be game.
	std::string state;
	if (ok && !QueryString(
	              L"typeof SystemHook != 'undefined' ? SystemHook.currentState "
	              L": ''",
	              state, "advance state")) {
		ok = false;
	} else if (state != "game") {
		ok = false;
	}

	// 2. The dialogue must be waiting for a click.
	if (ok && !QueryBool(L"(function(){ if (typeof kag == 'undefined' || typeof "
	                     L"kag.onPrimaryClick != 'Object') return 0;"
	                     L"if (typeof kag.clickWaiting == 'undefined' || "
	                     L"!kag.clickWaiting) return 0;"
	                     L"return 1; })()",
	                     ready, "advance click")) {
		ok = false;
	} else if (ok && !ready) {
		ok = false;
	}

	// 3. Dispatch the click.
	bool dispatched = false;
	if (ok) {
		dispatched = QueryBool(L"(function(){ kag.onPrimaryClick(); return 1; })()",
		                       dispatched, "advance dispatch");
	}

	if (!ok || !dispatched) {
		NeuroSendActionResult(id.c_str(), false, "Text advance is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Text advanced.");
}

void HandleSelectChoiceAction(std::string const &id, std::string const &data) {
	int index = -1;
	if (!ParseJsonInteger(data, "\"index\"", index) || index < 0) {
		NeuroSendActionResult(id.c_str(), false,
		                      "select_choice requires a non-negative integer "
		                      "\"index\".");
		return;
	}

	bool ok = true;
	bool ready = false;

	// 1. Current state must be game.
	std::string state;
	if (ok && !QueryString(
	              L"typeof SystemHook != 'undefined' ? SystemHook.currentState "
	              L": ''",
	              state, "select state")) {
		ok = false;
	} else if (state != "game") {
		ok = false;
	}

	// 2. The choice layer must be present, enabled, and non-empty.
	if (ok &&
	    !QueryBool(L"(function(){ if (typeof kag == 'undefined' || typeof "
	               L"kag.selectLayer == 'undefined') return 0;"
	               L"var layer = kag.selectLayer;"
	               L"if (layer.selects == void || layer.selects.count < 1) "
	               L"return 0;"
	               L"if (typeof layer.enabled != 'undefined' && !layer.enabled) "
	               L"return 0;"
	               L"return 1; })()",
	               ready, "select layer")) {
		ok = false;
	} else if (ok && !ready) {
		ok = false;
	}

	// 3. The requested index must exist and be clickable.
	if (ok) {
		wchar_t probe[512];
		swprintf_s(probe,
		           L"(function(){ if (typeof kag == 'undefined' || "
		           L"typeof kag.selectLayer == 'undefined') return 0;"
		           L"var layer = kag.selectLayer;"
		           L"if (layer.selects == void || layer.selects.count < "
		           L"%d) return 0;"
		           L"var button = layer.selects[%d];"
		           L"if (!isvalid button || (typeof button.enabled != "
		           L"'undefined' && !button.enabled)) return 0;"
		           L"return 1; })()",
		           index, index);
		if (!QueryBool(probe, ready, "select button")) {
			ok = false;
		} else if (!ready) {
			ok = false;
		}
	}

	// 4. Dispatch the button click.
	bool dispatched = false;
	if (ok) {
		wchar_t probe[512];
		swprintf_s(probe,
		           L"(function(){ var layer = kag.selectLayer;"
		           L"var button = layer.selects[%d];"
		           L"layer.onButtonClick(button); return 1; })()",
		           index);
		dispatched = QueryBool(probe, dispatched, "select dispatch");
	}

	if (!ok || !dispatched) {
		NeuroSendActionResult(id.c_str(), false, "Choice is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Choice selected.");
}

void HandleOpenQuickMenuAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "open_quick_menu does not accept arguments.");
		return;
	}

	bool ok = true;
	bool ready = false;

	std::string state;
	if (ok && !QueryString(
	              L"typeof SystemHook != 'undefined' ? SystemHook.currentState "
	              L": ''",
	              state, "quick_menu state")) {
		ok = false;
	} else if (state != "game") {
		ok = false;
	}

	// updown(1) is the real slide-out. The panel's own open()/close() are empty
	// stubs, and showing/visible read 1 even while it is retracted, so
	// _dropdown is the only usable state flag; see menu-navigation.md.
	if (ok && !QueryBool(L"(function(){ if (typeof kag == 'undefined' || typeof "
	                     L"kag.QuickMenu == 'undefined') return 0;"
	                     L"if (typeof kag.QuickMenu.updown != 'Object') return 0;"
	                     L"if (!kag.QuickMenu.canDropDown) return 0;"
	                     L"return 1; })()",
	                     ready, "quick_menu dispatchable")) {
		ok = false;
	} else if (ok && !ready) {
		ok = false;
	}

	bool dispatched = false;
	if (ok) {
		dispatched = QueryBool(
		    L"(function(){ kag.QuickMenu.updown(1); return kag.QuickMenu._dropdown "
		    L"? 1 : 0; })()",
		    dispatched, "quick_menu dispatch");
	}

	if (!ok || !dispatched) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the quick menu is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the quick menu.");
}

void HandleOpenMenuAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "open_menu does not accept arguments.");
		return;
	}

	bool ok = true;
	bool ready = false;

	// 1. Only from game: this is the quickmenu "Option" dispatch, and the
	//    settings screen already has go_back for the way out.
	std::string state;
	if (ok && !QueryString(
	              L"typeof SystemHook != 'undefined' ? SystemHook.currentState "
	              L": ''",
	              state, "open_menu state")) {
		ok = false;
	} else if (state != "game") {
		ok = false;
	}

	// 2. Press the quick menu's own Option button instead of calling
	//    SystemAction.option() directly. Same destination, but it is the control
	//    a player actually clicks, so the action is visible on screen rather
	//    than the settings appearing from nowhere.
	if (ok &&
	    !QueryBool(L"(function(){ if (typeof kag == 'undefined' || typeof "
	               L"kag.QuickMenu == 'undefined') return 0;"
	               L"var button = kag.QuickMenu.names['option'];"
	               L"if (typeof button == 'undefined' || !isvalid button || "
	               L"!button.visible || !button.enabled) return 0;"
	               L"if (typeof button.controlOwner == 'undefined' || typeof "
	               L"button.controlOwner.onButtonClick != 'Object') return 0;"
	               L"return 1; })()",
	               ready, "open_menu button")) {
		ok = false;
	} else if (ok && !ready) {
		ok = false;
	}

	bool dispatched = false;
	if (ok) {
		dispatched = QueryBool(
		    L"(function(){ var button = kag.QuickMenu.names['option'];"
		    L"button.controlOwner.onButtonClick(button.linkNum); return 1; })()",
		    dispatched, "open_menu dispatch");
	}

	if (!ok || !dispatched) {
		NeuroSendActionResult(id.c_str(), false,
		                      "Opening the settings menu is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Opened the settings menu.");
}

void HandleGoBackAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "go_back does not accept arguments.");
		return;
	}

	bool ok = true;
	bool ready = false;

	// 1. Current state must be one of the menu screens that have a plain
	//    "back" control (option, load, save, voice). Confirmed 2026-08-19:
	//    kag.current.names["back"] returns to the screen's origin without
	//    the "Back To Title" confirmation dialog documented separately.
	std::string state;
	if (ok && !QueryString(
	              L"typeof SystemHook != 'undefined' ? SystemHook.currentState "
	              L": ''",
	              state, "go_back state")) {
		ok = false;
	} else if (state != "option" && state != "load" && state != "save" &&
	           state != "voice") {
		ok = false;
	}

	// 2. The back button must be visible, enabled, and dispatchable.
	if (ok &&
	    !QueryBool(L"(function(){ if (typeof kag == 'undefined' || typeof "
	               L"kag.current == 'undefined' || typeof kag.current.names == "
	               L"'undefined') return 0;"
	               L"var button = kag.current.names['back'];"
	               L"if (typeof button == 'undefined' || !isvalid button || "
	               L"!button.visible || !button.enabled) return 0;"
	               L"if (typeof button.controlOwner == 'undefined' || typeof "
	               L"button.controlOwner.onButtonClick != 'Object') return 0;"
	               L"return 1; })()",
	               ready, "go_back button")) {
		ok = false;
	} else if (ok && !ready) {
		ok = false;
	}

	// 3. Dispatch the button click.
	bool dispatched = false;
	if (ok) {
		dispatched = QueryBool(
		    L"(function(){ var button = kag.current.names['back'];"
		    L"button.controlOwner.onButtonClick(button.linkNum); return 1; })()",
		    dispatched, "go_back dispatch");
	}

	if (!ok || !dispatched) {
		NeuroSendActionResult(id.c_str(), false, "Going back is unavailable.");
		return;
	}
	NeuroSendActionResult(id.c_str(), true, "Returned to the previous screen.");
}

void HandleNeuroAction(std::string const &id,
                       std::string const &name,
                       std::string const &data) {
	if (id.empty()) {
		Log("[Action] Ignoring action without an ID\n");
		return;
	}
	if (!ClaimActionId(id)) {
		Log("[Action] Ignoring duplicate action ID\n");
		return;
	}
	if (name == "continue_game") {
		HandleContinueAction(id, data);
	} else if (name == "advance_text") {
		HandleAdvanceTextAction(id, data);
	} else if (name == "select_choice") {
		HandleSelectChoiceAction(id, data);
	} else if (name == "open_menu") {
		HandleOpenMenuAction(id, data);
	} else if (name == "open_quick_menu") {
		HandleOpenQuickMenuAction(id, data);
	} else if (name == "go_back") {
		HandleGoBackAction(id, data);
	} else {
		NeuroSendActionResult(id.c_str(), false, "Unsupported action.");
	}
}

void SyncCapabilities(int state) {
	if (state == g_SyncedCapabilityState.load(std::memory_order_acquire)) {
		return;
	}
	if (state == kCapabilityStateTitleLoading) {
		// No action can succeed yet, so advertise none. The explanatory context
		// is not sent here: this state is entered during plugin init, before the
		// SDK connects, and NeuroSendContext drops anything sent while not ready.
		// RefreshPrototypeActionState retries it until it lands.
		NeuroSetRegisteredActions(nullptr, 0);
		Log("[Action] Capabilities: title-loading (none)\n");
	} else if (state == kCapabilityStateTitle) {
		NeuroRegisteredAction const actions[] = {
		    {"continue_game", "Continue the current game.", kContinueGameSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		// Not silent: the menu becoming usable is the moment there is finally
		// something to do.
		NeuroSendContext("The main menu is ready.", false);
		Log("[Action] Capabilities: title (continue_game)\n");
	} else if (state == kCapabilityStateGame) {
		NeuroRegisteredAction const actions[] = {
		    {"advance_text", "Advance the current dialogue.", kAdvanceTextSchema},
		    {"select_choice", "Choose a dialogue option by index.",
		     kSelectChoiceSchema},
		    {"open_menu", "Open the settings menu.", kOpenMenuSchema},
		    {"open_quick_menu",
		     "Slide out the quick menu bar so its buttons are visible.",
		     kOpenQuickMenuSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		Log("[Action] Capabilities: game (advance_text, select_choice, "
		    "open_menu, open_quick_menu)\n");
	} else if (state == kCapabilityStateMenu) {
		NeuroRegisteredAction const actions[] = {
		    {"go_back",
		     "Return to the previous screen from the current menu (options, "
		     "save, load, or voice).",
		     kGoBackSchema},
		};
		NeuroSetRegisteredActions(
		    actions, static_cast<int>(sizeof(actions) / sizeof(actions[0])));
		Log("[Action] Capabilities: menu (go_back)\n");
	}
	g_SyncedCapabilityState.store(state, std::memory_order_release);
}

}  // namespace

void InitializePrototypeActionRouter() {
	NeuroSetActionHandler(HandleNeuroAction);
	// Init runs during the startup splashes, so nothing is dispatchable yet;
	// the monitor promotes this to the real title state once the menu is usable.
	SyncCapabilities(kCapabilityStateTitleLoading);
}

void ShutdownPrototypeActionRouter() {
	NeuroSetActionHandler(nullptr);
	g_ContinueActionInFlight.store(false, std::memory_order_release);
	g_SyncedCapabilityState.store(kCapabilityStateUnknown,
	                              std::memory_order_release);
	g_TitleLoadingNoticeSent.store(false, std::memory_order_release);
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	g_ActionIds.clear();
	g_ActionIdSet.clear();
}

// Retried every poll until accepted: the loading state starts before the SDK
// connects, and NeuroSendContext drops anything sent while not ready rather
// than queueing it, so a single send on entering the state would be lost.
void TrySendTitleLoadingNotice() {
	if (g_TitleLoadingNoticeSent.load(std::memory_order_acquire)) {
		return;
	}
	if (NeuroSendContext(
	        "The game is starting up. The main menu is not available yet.",
	        true)) {
		g_TitleLoadingNoticeSent.store(true, std::memory_order_release);
	}
}

void RefreshPrototypeActionState() {
	tTJSVariant result;
	bool const executed = ExecuteTJSSync(
	    L"(function(){ return typeof SystemHook != 'undefined' ? "
	    L"SystemHook.currentState : ''; })()",
	    &result, 5000, TJSExecMode::Expression);
	if (!executed) {
		Log("[Action] Refresh state probe failed\n");
		return;
	}

	ttstr value(result);
	std::string state;
	bool const converted = Utf16ToUtf8(value.c_str(), value.GetLen(), state);
	if (!converted) {
		Log("[Action] Refresh state conversion failed\n");
		return;
	}

	// A state this function does not recognise falls through every branch below
	// and returns without a trace, which is indistinguishable from "capabilities
	// are simply unchanged". Log each distinct value once so an unexpected or
	// empty state is visible instead of silent.
	{
		static std::string lastObservedState;
		if (state != lastObservedState) {
			lastObservedState = state;
			Log(("[Action] Observed state: '" + state + "'\n").c_str());
		}
	}

	if (state == "title") {
		// Register continue_game only once the control it dispatches is actually
		// usable, matching the guards the action itself runs. Probed only while
		// it is not yet registered, so the steady state costs nothing extra.
		if (g_SyncedCapabilityState.load(std::memory_order_acquire) ==
		    kCapabilityStateTitle) {
			SyncCapabilities(kCapabilityStateTitle);
		} else {
			bool canContinue = false;
			bool buttonReady = false;
			if (QueryBool(L"typeof SystemAction != 'undefined' && "
			              L"SystemAction.canContinue ? 1 : 0",
			              canContinue, "title canContinue") &&
			    canContinue &&
			    QueryBool(
			        L"(function(){ if (typeof kag == 'undefined' || typeof "
			        L"kag.current == 'undefined' || typeof kag.current.names == "
			        L"'undefined') return 0;"
			        L"var button = kag.current.names['cont'];"
			        L"if (typeof button == 'undefined' || !isvalid button || "
			        L"!button.visible || !button.enabled) return 0;"
			        L"if (typeof button.controlOwner == 'undefined' || typeof "
			        L"button.controlOwner.onButtonClick != 'Object') return 0;"
			        L"return 1; })()",
			        buttonReady, "title button") &&
			    buttonReady) {
				SyncCapabilities(kCapabilityStateTitle);
			} else {
				SyncCapabilities(kCapabilityStateTitleLoading);
				TrySendTitleLoadingNotice();
			}
		}
	} else if (state.empty()) {
		// Still in the startup splashes: SystemHook.currentState is genuinely an
		// empty string until the title exists. This is the bulk of the wait, so
		// the notice has to cover it -- restricting it to the brief
		// title-but-not-ready window made it arrive just as the menu appeared,
		// which read as nonsense ("not available yet" the moment it was).
		//
		// Only believe an empty reading before any real state has been seen.
		// currentState is not a stable field: it was observed holding unrelated
		// values mid-session (e.g. "bgmPlaying" while the option screen was up),
		// so treating a stray empty reading as "back to loading" would revoke the
		// live action set for no reason.
		int const synced = g_SyncedCapabilityState.load(std::memory_order_acquire);
		if (synced == kCapabilityStateUnknown ||
		    synced == kCapabilityStateTitleLoading) {
			SyncCapabilities(kCapabilityStateTitleLoading);
			TrySendTitleLoadingNotice();
		}
	} else if (state == "game") {
		SyncCapabilities(kCapabilityStateGame);
	} else if (state == "option" || state == "load" || state == "save" ||
	           state == "voice") {
		SyncCapabilities(kCapabilityStateMenu);
	}

	if (!g_ContinueActionInFlight.load(std::memory_order_acquire)) {
		return;
	}
	if (state != "title" || GetTickCount64() - g_ContinueActionStartedAt.load(
	                                               std::memory_order_acquire) >=
	                            5000) {
		g_ContinueActionInFlight.store(false, std::memory_order_release);
		Log("[Action] Continue transition completed or timed out\n");
	}
}
