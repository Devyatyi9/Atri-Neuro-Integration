#include "NeuroClient.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <neurosdk.h>

#include "Logging.h"
#include "ThreadGuard.h"

namespace {
constexpr unsigned int kReconnectInitialMs = 500;
constexpr unsigned int kReconnectMaxMs = 30000;

struct RegisteredAction {
	std::string name;
	std::string description;
	std::string jsonSchema;
};

neurosdk_context_t g_NeuroContext = nullptr;
std::mutex g_ContextMutex;
std::atomic<bool> g_NeuroConnected(false);
std::atomic<bool> g_NeuroReady(false);
std::atomic<bool> g_NeuroPollThreadRunning(false);
std::thread g_NeuroPollThread;
std::mutex g_ConfigurationMutex;
std::string g_NeuroGameName = "ATRI-MyDearMoments";
std::string g_NeuroURL = "ws://localhost:8000";

std::vector<RegisteredAction> g_RegisteredActions;
std::mutex g_ActionsMutex;

// Context is queued here and flushed by the poll thread, never sent from the
// caller's thread. The poll thread reacquires g_ContextMutex every poll_ms in a
// tight loop, and std::mutex is not fair on Windows, so a caller that blocks on
// it can starve indefinitely -- that is exactly what stalled the 200ms context
// monitor loop when it called the send path directly (2026-08-19).
struct OutboundContext {
	std::string message;
	bool silent;
};
constexpr size_t kMaxQueuedContext = 64;
std::vector<OutboundContext> g_OutboundContext;
std::mutex g_OutboundContextMutex;
NeuroActionHandler g_ActionHandler = nullptr;
std::mutex g_ActionHandlerMutex;

void LogError(char const *operation, neurosdk_error_e error) {
	char message[256];
	sprintf_s(message, "[Neuro] %s: %s\n", operation,
	          neurosdk_error_string(error));
	Log(message);
}

void NeuroLogCallback(neurosdk_severity_e severity,
                      char *message,
                      void *userData) {
	(void)userData;
	char const *severityName = "unknown";
	switch (severity) {
		case NeuroSDK_Severity_Debug:
			severityName = "debug";
			break;
		case NeuroSDK_Severity_Info:
			severityName = "info";
			break;
		case NeuroSDK_Severity_Warn:
			severityName = "warn";
			break;
		case NeuroSDK_Severity_Error:
			severityName = "error";
			break;
	}
	char log[1024];
	sprintf_s(log, "[NeuroSDK %s] %s\n", severityName,
	          message ? message : "(null)");
	Log(log);
}

bool SleepUntilRetry(unsigned int milliseconds) {
	while (g_NeuroPollThreadRunning.load(std::memory_order_acquire) &&
	       milliseconds > 0) {
		unsigned int const interval = milliseconds > 100 ? 100 : milliseconds;
		Sleep(interval);
		milliseconds -= interval;
	}
	return g_NeuroPollThreadRunning.load(std::memory_order_acquire);
}

unsigned int NextBackoff(unsigned int current) {
	return current >= kReconnectMaxMs / 2 ? kReconnectMaxMs : current * 2;
}

neurosdk_error_e SendMessage(neurosdk_message_t *message) {
	std::lock_guard<std::mutex> lock(g_ContextMutex);
	if (!g_NeuroContext) {
		return NeuroSDK_Uninitialized;
	}
	return neurosdk_context_send(&g_NeuroContext, message);
}

neurosdk_error_e SendStartup() {
	neurosdk_message_t startup = {};
	startup.kind = NeuroSDK_MessageKind_Startup;
	return SendMessage(&startup);
}

neurosdk_error_e SendRegisteredActions() {
	std::vector<RegisteredAction> actions;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		actions = g_RegisteredActions;
	}
	if (actions.empty()) {
		return NeuroSDK_None;
	}

	std::vector<neurosdk_action_t> sdkActions;
	sdkActions.reserve(actions.size());
	for (RegisteredAction &action : actions) {
		sdkActions.push_back({const_cast<char *>(action.name.c_str()),
		                      const_cast<char *>(action.description.c_str()),
		                      const_cast<char *>(action.jsonSchema.c_str())});
	}
	neurosdk_message_t message = {};
	message.kind = NeuroSDK_MessageKind_ActionsRegister;
	message.value.actions_register.actions = sdkActions.data();
	message.value.actions_register.actions_len =
	    static_cast<int>(sdkActions.size());
	return SendMessage(&message);
}

// `actions/register` is additive on the server: it adds to whatever the server
// already knows and never implies removal. Dropping an action therefore needs
// an explicit `actions/unregister`, exactly as the SDK's own reference client
// does (lib/neurosdk/examples/rock_paper_scissors.c uses both messages).
// Re-sending a shorter register list is not enough -- the server keeps the
// stale action.
neurosdk_error_e SendUnregisterActions(std::vector<std::string> const &names) {
	if (names.empty()) {
		return NeuroSDK_None;
	}
	std::vector<char *> sdkNames;
	sdkNames.reserve(names.size());
	for (std::string const &name : names) {
		sdkNames.push_back(const_cast<char *>(name.c_str()));
	}
	neurosdk_message_t message = {};
	message.kind = NeuroSDK_MessageKind_ActionsUnregister;
	message.value.actions_unregister.action_names = sdkNames.data();
	message.value.actions_unregister.action_names_len =
	    static_cast<int>(sdkNames.size());
	return SendMessage(&message);
}

// Runs on the poll thread, which owns the SDK context.
void FlushOutboundContext() {
	std::vector<OutboundContext> pending;
	{
		std::lock_guard<std::mutex> lock(g_OutboundContextMutex);
		pending.swap(g_OutboundContext);
	}
	for (OutboundContext const &entry : pending) {
		neurosdk_message_t message = {};
		message.kind = NeuroSDK_MessageKind_Context;
		message.value.context.message = const_cast<char *>(entry.message.c_str());
		message.value.context.silent = entry.silent;
		neurosdk_error_e const error = SendMessage(&message);
		if (error != NeuroSDK_None) {
			LogError("Send context failed", error);
			return;
		}
	}
}

void DispatchAction(std::string const &id,
                    std::string const &name,
                    std::string const &data) {
	NeuroActionHandler handler = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_ActionHandlerMutex);
		handler = g_ActionHandler;
	}
	if (handler) {
		handler(id, name, data);
	} else {
		Log("[Neuro] Ignoring action: no handler installed\n");
	}
}

neurosdk_error_e ProcessMessages(neurosdk_message_t *messages, int count) {
	neurosdk_error_e result = NeuroSDK_None;
	for (int index = 0; index < count; ++index) {
		neurosdk_message_t *message = &messages[index];
		if (result == NeuroSDK_None) {
			switch (message->kind) {
				case NeuroSDK_MessageKind_Action: {
					std::string const id =
					    message->value.action.id ? message->value.action.id : "";
					std::string const name =
					    message->value.action.name ? message->value.action.name : "";
					std::string const data =
					    message->value.action.data ? message->value.action.data : "";
					DispatchAction(id, name, data);
					break;
				}
				case NeuroSDK_MessageKind_ActionsReregisterAll:
					Log("[Neuro] Re-registering current actions\n");
					result = SendRegisteredActions();
					if (result != NeuroSDK_None) {
						LogError("Re-register actions failed", result);
					}
					break;
				case NeuroSDK_MessageKind_StartupAcknowledgement:
					g_NeuroReady.store(true, std::memory_order_release);
					Log("[Neuro] Startup acknowledged\n");
					break;
				default:
					break;
			}
		}
		neurosdk_message_destroy(message);
	}
	return result;
}

void DestroyContext() {
	std::lock_guard<std::mutex> lock(g_ContextMutex);
	if (g_NeuroContext) {
		neurosdk_context_destroy(&g_NeuroContext);
	}
}

bool CreateSession() {
	std::string url;
	std::string gameName;
	{
		std::lock_guard<std::mutex> lock(g_ConfigurationMutex);
		url = g_NeuroURL;
		gameName = g_NeuroGameName;
	}

	neurosdk_context_create_desc_t description = {};
	description.url = url.c_str();
	description.game_name = gameName.c_str();
	description.poll_ms = 100;
	description.callback_log = NeuroLogCallback;
	description.flags = NeuroSDK_ContextCreateFlags_None;
	{
		std::lock_guard<std::mutex> lock(g_ContextMutex);
		neurosdk_error_e const error =
		    neurosdk_context_create(&g_NeuroContext, &description);
		if (error != NeuroSDK_None) {
			LogError("Connection failed", error);
			return false;
		}
	}

	neurosdk_error_e error = SendStartup();
	if (error == NeuroSDK_None) {
		error = SendRegisteredActions();
	}
	if (error != NeuroSDK_None) {
		LogError("Session initialization failed", error);
		DestroyContext();
		return false;
	}

	g_NeuroConnected.store(true, std::memory_order_release);
	Log("[Neuro] Connected; awaiting startup acknowledgement\n");
	return true;
}

void NeuroPollThread() {
	unsigned int backoffMs = kReconnectInitialMs;
	Log("[Neuro] Poll thread started\n");
	while (g_NeuroPollThreadRunning.load(std::memory_order_acquire)) {
		g_NeuroReady.store(false, std::memory_order_release);
		if (!CreateSession()) {
			if (!SleepUntilRetry(backoffMs)) {
				break;
			}
			backoffMs = NextBackoff(backoffMs);
			continue;
		}
		backoffMs = kReconnectInitialMs;

		neurosdk_error_e error = NeuroSDK_None;
		while (g_NeuroPollThreadRunning.load(std::memory_order_acquire) &&
		       error == NeuroSDK_None) {
			neurosdk_message_t *messages = nullptr;
			int count = 0;
			{
				std::lock_guard<std::mutex> lock(g_ContextMutex);
				if (!g_NeuroContext || !neurosdk_context_connected(&g_NeuroContext)) {
					error = NeuroSDK_ConnectionError;
				} else {
					error = neurosdk_context_poll(&g_NeuroContext, &messages, &count);
				}
			}
			if (error == NeuroSDK_None) {
				error = ProcessMessages(messages, count);
			}
			if (error == NeuroSDK_None) {
				FlushOutboundContext();
			}
		}

		DestroyContext();
		g_NeuroConnected.store(false, std::memory_order_release);
		g_NeuroReady.store(false, std::memory_order_release);
		if (g_NeuroPollThreadRunning.load(std::memory_order_acquire)) {
			LogError("Disconnected", error);
			if (!SleepUntilRetry(backoffMs)) {
				break;
			}
			backoffMs = NextBackoff(backoffMs);
		}
	}
	DestroyContext();
	g_NeuroConnected.store(false, std::memory_order_release);
	g_NeuroReady.store(false, std::memory_order_release);
	Log("[Neuro] Poll thread stopped\n");
}
}  // namespace

void NeuroSetActionHandler(NeuroActionHandler handler) {
	std::lock_guard<std::mutex> lock(g_ActionHandlerMutex);
	g_ActionHandler = handler;
}

bool NeuroConnect(char const *url, char const *gameName) {
	if (g_NeuroPollThreadRunning.exchange(true, std::memory_order_acq_rel)) {
		return true;
	}
	if (g_NeuroPollThread.joinable()) {
		g_NeuroPollThread.join();
	}
	{
		std::lock_guard<std::mutex> lock(g_ConfigurationMutex);
		if (url && *url) {
			g_NeuroURL = url;
		}
		if (gameName && *gameName) {
			g_NeuroGameName = gameName;
		}
	}
	g_NeuroPollThread =
	    std::thread([] { RunThreadGuarded("neuro poll", NeuroPollThread); });
	return true;
}

void NeuroAutoConnect() {
	if (NeuroIsConnected() || g_NeuroPollThreadRunning.load()) {
		return;
	}

	char const *env = std::getenv("NEURO_AUTOCONNECT");
	if (env && env[0] == '0') {
		Log("[Neuro] Autoconnect disabled (NEURO_AUTOCONNECT=0)\n");
		return;
	}

	char const *wsUrl = std::getenv("NEURO_WS_URL");
	std::string url = (wsUrl && *wsUrl) ? wsUrl : "ws://localhost:8000";
	Log(("[Neuro] Autoconnecting to " + url + "\n").c_str());
	NeuroConnect(url.c_str(), nullptr);
}

void NeuroDisconnect() {
	g_NeuroPollThreadRunning.store(false, std::memory_order_release);
	if (g_NeuroPollThread.joinable()) {
		g_NeuroPollThread.join();
	}
	DestroyContext();
	g_NeuroConnected.store(false, std::memory_order_release);
	g_NeuroReady.store(false, std::memory_order_release);
}

bool NeuroIsConnected() {
	return g_NeuroConnected.load(std::memory_order_acquire);
}

bool NeuroIsReady() {
	return g_NeuroReady.load(std::memory_order_acquire);
}

bool NeuroSendContext(char const *message, bool silent) {
	if (!message || !NeuroIsReady()) {
		return false;
	}
	// Queue only; the poll thread does the actual send. Callers are game-thread
	// or polling-loop code that must never block on the SDK context mutex, so
	// this returns immediately and reports "accepted", not "delivered".
	std::lock_guard<std::mutex> lock(g_OutboundContextMutex);
	if (g_OutboundContext.size() >= kMaxQueuedContext) {
		// Prefer the newest context: an unbounded backlog would only replay
		// stale scene descriptions once the connection recovers.
		g_OutboundContext.erase(g_OutboundContext.begin());
	}
	g_OutboundContext.push_back({message, silent});
	return true;
}

bool NeuroSetRegisteredActions(NeuroRegisteredAction const *actions,
                               int actionCount) {
	std::vector<std::string> droppedNames;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		// Names present before but absent from the new set have to be revoked
		// explicitly; see SendUnregisterActions.
		//
		// A name that stays but whose description or schema changed has to be
		// revoked too. Registering an action that is already registered is
		// ignored by the server ("If you try to register an action that is
		// already registered, it will be ignored" -- neuro-sdk/API/
		// SPECIFICATION.md), so an updated description or schema would never
		// reach Neuro otherwise. That is not hypothetical: view_cg narrows its
		// square range on the gallery's last page, and the old range kept
		// standing until this dropped it first (2026-08-20).
		for (RegisteredAction const &existing : g_RegisteredActions) {
			bool unchanged = false;
			for (int index = 0; index < actionCount; ++index) {
				char const *const name = actions[index].name;
				if (!name || existing.name != name) {
					continue;
				}
				char const *const description = actions[index].description;
				char const *const schema = actions[index].jsonSchema;
				unchanged = existing.description == (description ? description : "") &&
				            existing.jsonSchema == (schema ? schema : "{}");
				break;
			}
			if (!unchanged) {
				droppedNames.push_back(existing.name);
			}
		}
		g_RegisteredActions.clear();
		for (int index = 0; index < actionCount; ++index) {
			g_RegisteredActions.push_back(
			    {actions[index].name ? actions[index].name : "",
			     actions[index].description ? actions[index].description : "",
			     actions[index].jsonSchema ? actions[index].jsonSchema : "{}"});
		}
	}
	if (!NeuroIsConnected()) {
		return true;
	}
	// Unregister first: the server then never observes a superset of both the
	// outgoing and incoming action sets.
	neurosdk_error_e const unregisterError = SendUnregisterActions(droppedNames);
	if (unregisterError != NeuroSDK_None) {
		LogError("Unregister dropped actions failed", unregisterError);
	}
	neurosdk_error_e const error = SendRegisteredActions();
	if (error != NeuroSDK_None) {
		LogError("Set actions failed", error);
		return false;
	}
	return true;
}

bool NeuroRegisterAction(char const *name,
                         char const *description,
                         char const *jsonSchema) {
	if (!name || !*name || !description) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		g_RegisteredActions.push_back(
		    {name, description, jsonSchema ? jsonSchema : "{}"});
	}
	if (!NeuroIsConnected()) {
		return true;
	}
	neurosdk_error_e const error = SendRegisteredActions();
	if (error != NeuroSDK_None) {
		LogError("Register action failed", error);
		return false;
	}
	return true;
}

bool NeuroUnregisterAction(char const *name) {
	if (!name || !*name) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		auto &actions = g_RegisteredActions;
		actions.erase(std::remove_if(actions.begin(), actions.end(),
		                             [&](RegisteredAction const &action) {
			                             return action.name == name;
		                             }),
		              actions.end());
	}
	if (!NeuroIsConnected()) {
		return true;
	}
	// Revoke it on the server as well; re-sending the shorter register list
	// alone would leave the action live there (see SendUnregisterActions).
	neurosdk_error_e const error = SendUnregisterActions({name});
	if (error != NeuroSDK_None) {
		LogError("Unregister action failed", error);
		return false;
	}
	return true;
}

bool NeuroReregisterActions() {
	if (!NeuroIsConnected()) {
		return true;
	}
	neurosdk_error_e const error = SendRegisteredActions();
	if (error != NeuroSDK_None) {
		LogError("Re-register actions failed", error);
		return false;
	}
	return true;
}

bool NeuroSendActionResult(char const *id, bool success, char const *message) {
	if (!id || !NeuroIsReady()) {
		return false;
	}
	neurosdk_message_t result = {};
	result.kind = NeuroSDK_MessageKind_ActionResult;
	result.value.action_result.id = const_cast<char *>(id);
	result.value.action_result.success = success;
	result.value.action_result.message =
	    const_cast<char *>(message ? message : "");
	neurosdk_error_e const error = SendMessage(&result);
	if (error != NeuroSDK_None) {
		LogError("Send action result failed", error);
		return false;
	}
	return true;
}

bool NeuroForceAction(char const *state,
                      char const *query,
                      char const **actionNames,
                      int actionCount,
                      bool ephemeral) {
	if (!NeuroIsReady()) {
		return false;
	}
	neurosdk_message_t request = {};
	request.kind = NeuroSDK_MessageKind_ActionsForce;
	request.value.actions_force.state = const_cast<char *>(state ? state : "");
	request.value.actions_force.query = const_cast<char *>(query ? query : "");
	request.value.actions_force.ephemeral_context = ephemeral;
	request.value.actions_force.action_names = const_cast<char **>(actionNames);
	request.value.actions_force.action_names_len = actionCount;
	request.value.actions_force.priority = NeuroSDK_Priority_Medium;
	neurosdk_error_e const error = SendMessage(&request);
	if (error != NeuroSDK_None) {
		LogError("Force action failed", error);
		return false;
	}
	return true;
}
