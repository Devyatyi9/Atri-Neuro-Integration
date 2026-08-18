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
#include "UtfConversion.h"

namespace {

std::atomic<bool> g_ContinueActionInFlight(false);
std::atomic<ULONGLONG> g_ContinueActionStartedAt(0);
std::mutex g_ActionIdsMutex;
std::deque<std::string> g_ActionIds;
std::unordered_set<std::string> g_ActionIdSet;
constexpr size_t kMaxRememberedActionIds = 256;

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

	// This expression is fixed native code. Incoming action data is never
	// interpolated into TJS source.
	wchar_t const *expression =
	    L"(function(){"
	    L"try {"
	    L"if (typeof SystemHook == 'undefined' || SystemHook.currentState != "
	    L"'title') return 'not at title';"
	    L"if (typeof SystemAction == 'undefined' || !SystemAction.canContinue) "
	    L"return 'continue unavailable';"
	    L"if (typeof Current == 'undefined' || !Current.valid) return 'title not "
	    L"ready';"
	    L"if (typeof kag == 'undefined' || typeof kag.current == 'undefined' || "
	    L"typeof kag.current.names == 'undefined') return 'title UI unavailable';"
	    L"var button = kag.current.names['cont'];"
	    L"if (typeof button == 'undefined' || !isvalid button || !button.visible "
	    L"|| !button.enabled) return 'continue button unavailable';"
	    L"if (typeof button.controlOwner == 'undefined' || typeof "
	    L"button.controlOwner.onButtonClick != 'Object') return 'continue "
	    L"dispatch unavailable';"
	    L"button.controlOwner.onButtonClick(button.linkNum);"
	    L"return 'started';"
	    L"} catch(e) { return 'dispatch failed'; }"
	    L"})()";
	tTJSVariant result;
	bool const executed = ExecuteTJSSync(expression, &result, 5000);
	if (!executed) {
		g_ContinueActionInFlight.store(false, std::memory_order_release);
		NeuroSendActionResult(id.c_str(), false, "Game thread did not respond.");
		return;
	}

	ttstr value(result);
	std::string message;
	bool const converted = Utf16ToUtf8(value.c_str(), value.GetLen(), message);
	bool const success = converted && message == "started";
	if (!success) {
		g_ContinueActionInFlight.store(false, std::memory_order_release);
	}
	NeuroSendActionResult(
	    id.c_str(), success,
	    success ? "Continue started." : "Continue is unavailable.");
}

void HandleAdvanceTextAction(std::string const &id, std::string const &data) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(id.c_str(), false,
		                      "advance_text does not accept arguments.");
		return;
	}

	// This expression is fixed native code. Incoming action data is never
	// interpolated into TJS source.
	wchar_t const *expression =
	    L"(function(){"
	    L"try {"
	    L"if (typeof SystemHook == 'undefined' || SystemHook.currentState != "
	    L"'game') return 'not in game';"
	    L"if (typeof kag == 'undefined' || typeof kag.onPrimaryClick != "
	    L"'Object') return 'click dispatch unavailable';"
	    L"if (typeof kag.clickWaiting == 'undefined' || !kag.clickWaiting) "
	    L"return 'not waiting for click';"
	    L"kag.onPrimaryClick();"
	    L"return 'advanced';"
	    L"} catch(e) { return 'dispatch failed'; }"
	    L"})()";
	tTJSVariant result;
	bool const executed = ExecuteTJSSync(expression, &result, 5000);
	if (!executed) {
		NeuroSendActionResult(id.c_str(), false, "Game thread did not respond.");
		return;
	}

	ttstr value(result);
	std::string message;
	bool const converted = Utf16ToUtf8(value.c_str(), value.GetLen(), message);
	bool const success = converted && message == "advanced";
	NeuroSendActionResult(
	    id.c_str(), success,
	    success ? "Text advanced." : "Text advance is unavailable.");
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

}  // namespace

void InitializePrototypeActionRouter() {
	NeuroSetActionHandler(HandleNeuroAction);
	if (NeuroRegisterAction("continue_game", "Continue the current game.",
	                        "{\"type\":\"object\",\"properties\":{},"
	                        "\"additionalProperties\":false}")) {
		Log("[Action] Registered continue_game capability\n");
	} else {
		Log("[Action] Failed to register continue_game capability\n");
	}
	if (NeuroRegisterAction("advance_text", "Advance the current dialogue.",
	                        "{\"type\":\"object\",\"properties\":{},"
	                        "\"additionalProperties\":false}")) {
		Log("[Action] Registered advance_text capability\n");
	} else {
		Log("[Action] Failed to register advance_text capability\n");
	}
}

void ShutdownPrototypeActionRouter() {
	NeuroSetActionHandler(nullptr);
	g_ContinueActionInFlight.store(false, std::memory_order_release);
	std::lock_guard<std::mutex> lock(g_ActionIdsMutex);
	g_ActionIds.clear();
	g_ActionIdSet.clear();
}

void RefreshPrototypeActionState() {
	if (!g_ContinueActionInFlight.load(std::memory_order_acquire)) {
		return;
	}
	tTJSVariant result;
	bool const executed = ExecuteTJSSync(
	    L"(function(){ return typeof SystemHook != 'undefined' && "
	    L"SystemHook.currentState != 'title'; })()",
	    &result, 5000);
	if ((executed && (tTVInteger)result) ||
	    GetTickCount64() -
	            g_ContinueActionStartedAt.load(std::memory_order_acquire) >=
	        5000) {
		g_ContinueActionInFlight.store(false, std::memory_order_release);
		Log("[Action] Continue transition completed or timed out\n");
	}
}
