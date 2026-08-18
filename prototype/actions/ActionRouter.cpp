#include "ActionRouter.h"

#include <windows.h>

#include <atomic>
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
constexpr int kCapabilityStateUnknown = -1;
constexpr int kCapabilityStateTitle = 0;
constexpr int kCapabilityStateGame = 1;

char const *kContinueGameSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";
char const *kAdvanceTextSchema =
    "{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}";

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
	if (first == std::string::npos || data[first] != '{') {
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
	} else {
		NeuroSendActionResult(id.c_str(), false, "Unsupported action.");
	}
}

void SyncCapabilities(int state) {
	if (state == g_SyncedCapabilityState.load(std::memory_order_acquire)) {
		return;
	}
	if (state == kCapabilityStateTitle) {
		NeuroRegisterAction("continue_game", "Continue the current game.",
		                    kContinueGameSchema);
		NeuroUnregisterAction("advance_text");
		Log("[Action] Capabilities: title (continue_game)\n");
	} else if (state == kCapabilityStateGame) {
		NeuroUnregisterAction("continue_game");
		NeuroRegisterAction("advance_text", "Advance the current dialogue.",
		                    kAdvanceTextSchema);
		Log("[Action] Capabilities: game (advance_text)\n");
	}
	g_SyncedCapabilityState.store(state, std::memory_order_release);
}

}  // namespace

void InitializePrototypeActionRouter() {
	NeuroSetActionHandler(HandleNeuroAction);
	SyncCapabilities(kCapabilityStateTitle);
}

void ShutdownPrototypeActionRouter() {
	NeuroSetActionHandler(nullptr);
	g_ContinueActionInFlight.store(false, std::memory_order_release);
	g_SyncedCapabilityState.store(kCapabilityStateUnknown,
	                              std::memory_order_release);
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	g_ActionIds.clear();
	g_ActionIdSet.clear();
}

void RefreshPrototypeActionState() {
	tTJSVariant result;
	bool const executed = ExecuteTJSSync(
	    L"(function(){ return typeof SystemHook != 'undefined' ? "
	    L"SystemHook.currentState : ''; })()",
	    &result, 5000);
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

	if (state == "title") {
		SyncCapabilities(kCapabilityStateTitle);
	} else if (state == "game") {
		SyncCapabilities(kCapabilityStateGame);
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
