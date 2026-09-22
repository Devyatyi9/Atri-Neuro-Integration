#include "NeuroClient.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <neurosdk.h>

#include "ActionTiming.h"
#include "Logging.h"
#include "PluginConfig.h"
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
// it can starve indefinitely.
struct OutboundContext {
	std::string message;
	bool silent;
};
constexpr size_t kMaxQueuedContext = 64;
std::vector<OutboundContext> g_OutboundContext;
std::mutex g_OutboundContextMutex;

// All protocol messages (action results, force actions, register/unregister)
// are queued and sent by the poll thread. Callers return immediately without
// blocking on g_ContextMutex.
struct OutboundMessage {
	bool isActionsSnapshot;
	neurosdk_message_kind_e kind;
	std::string id;
	std::string name;
	std::string description;
	std::string jsonSchema;
	std::string message;
	std::string state;
	std::string query;
	std::vector<std::string> actionNames;
	std::vector<RegisteredAction> actions;
	bool success;
	bool ephemeral;
};
constexpr size_t kMaxQueuedMessages = 128;
std::vector<OutboundMessage> g_OutboundMessages;
std::mutex g_OutboundMessagesMutex;

NeuroActionHandler g_ActionHandler = nullptr;
std::mutex g_ActionHandlerMutex;

// Actions handed to the game and not yet answered. A link that drops takes
// these with it: the id belonged to the session that is gone, and the answer
// can never reach the server that asked. Kept so the log can say how many were
// lost rather than leaving it to be worked out from two records afterwards.
std::set<std::string> g_UnansweredActions;
std::mutex g_UnansweredActionsMutex;
NeuroSessionHandler g_SessionHandler = nullptr;

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

// Makes room for one more outbound message, and says what it cost.
//
// An action result is the one message Neuro is waiting on: without it the
// action never completes at her end. So the victim is chosen rather than
// taken from the front -- the oldest message that is not a result goes
// first, and a result is only dropped when the queue holds nothing else.
void MakeRoomForOutboundMessage() {
	if (g_OutboundMessages.size() < kMaxQueuedMessages) {
		return;
	}
	for (size_t index = 0; index < g_OutboundMessages.size(); ++index) {
		if (g_OutboundMessages[index].kind == NeuroSDK_MessageKind_ActionResult) {
			continue;
		}
		Log("[Neuro] Outbound queue full, dropped a queued message\n");
		g_OutboundMessages.erase(g_OutboundMessages.begin() +
		                         static_cast<std::ptrdiff_t>(index));
		return;
	}
	// Nothing but results left, so one of them has to go, and the action it
	// belongs to will never complete. Logged loudly for that reason.
	Log(("[Neuro] Outbound queue full of results, dropping the result for " +
	     g_OutboundMessages.front().id + "\n")
	        .c_str());
	g_OutboundMessages.erase(g_OutboundMessages.begin());
}

struct ActionSnapshot {
	std::vector<RegisteredAction> actions;
	std::vector<std::string> previousNames;
};

void QueueActionsSnapshot(ActionSnapshot snapshot) {
	OutboundMessage message = {};
	message.isActionsSnapshot = true;
	message.actionNames = std::move(snapshot.previousNames);
	message.actions = std::move(snapshot.actions);
	std::lock_guard<std::mutex> lock(g_OutboundMessagesMutex);
	for (size_t index = 0; index < g_OutboundMessages.size(); ++index) {
		if (!g_OutboundMessages[index].isActionsSnapshot) {
			continue;
		}
		// Keep the names from the first pending snapshot: they represent the
		// server-visible set, while the replacement carries only the newest state.
		message.actionNames = g_OutboundMessages[index].actionNames;
		g_OutboundMessages[index] = std::move(message);
		g_OutboundMessages.erase(
		    std::remove_if(
		        g_OutboundMessages.begin() + static_cast<std::ptrdiff_t>(index) + 1,
		        g_OutboundMessages.end(),
		        [](OutboundMessage const &entry) {
			        return entry.isActionsSnapshot;
		        }),
		    g_OutboundMessages.end());
		return;
	}
	MakeRoomForOutboundMessage();
	g_OutboundMessages.push_back(std::move(message));
}

// What a failed send leaves behind. The queue is taken whole before sending,
// so everything after the entry that failed is dropped with the local copy --
// and until this was written, dropped in silence. Neuro's side of that silence
// is an action she was answered for and never heard back about.
void ReportAbandoned(std::vector<OutboundMessage> const &pending,
                     std::size_t from) {
	std::size_t results = 0;
	std::size_t others = 0;
	for (std::size_t index = from; index < pending.size(); ++index) {
		if (pending[index].kind == NeuroSDK_MessageKind_ActionResult) {
			results += 1;
		} else {
			others += 1;
		}
	}
	if (results == 0 && others == 0) {
		return;
	}
	char line[144];
	sprintf_s(line,
	          "[Neuro] The failed send abandoned %zu result(s) and %zu other "
	          "message(s)\n",
	          results, others);
	Log(line);
}

// Runs on the poll thread, which owns the SDK context.
void FlushOutboundContext() {
	std::vector<OutboundContext> pending;
	{
		std::lock_guard<std::mutex> lock(g_OutboundContextMutex);
		pending.swap(g_OutboundContext);
	}
	for (std::size_t index = 0; index < pending.size(); ++index) {
		OutboundContext const &entry = pending[index];
		neurosdk_message_t message = {};
		message.kind = NeuroSDK_MessageKind_Context;
		message.value.context.message = const_cast<char *>(entry.message.c_str());
		message.value.context.silent = entry.silent;
		neurosdk_error_e const error = SendMessage(&message);
		if (error != NeuroSDK_None) {
			LogError("Send context failed", error);
			char line[96];
			sprintf_s(line,
			          "[Neuro] The failed send abandoned %zu context "
			          "message(s)\n",
			          pending.size() - index);
			Log(line);
			return;
		}
	}
}

void FlushOutboundMessages() {
	std::vector<OutboundMessage> pending;
	{
		std::lock_guard<std::mutex> lock(g_OutboundMessagesMutex);
		pending.swap(g_OutboundMessages);
	}

	for (std::size_t index = 0; index < pending.size(); ++index) {
		OutboundMessage const &entry = pending[index];
		if (entry.isActionsSnapshot) {
			if (!entry.actionNames.empty()) {
				std::vector<char *> names;
				names.reserve(entry.actionNames.size());
				for (std::string const &name : entry.actionNames) {
					names.push_back(const_cast<char *>(name.c_str()));
				}
				neurosdk_message_t unregisterMessage = {};
				unregisterMessage.kind = NeuroSDK_MessageKind_ActionsUnregister;
				unregisterMessage.value.actions_unregister.action_names = names.data();
				unregisterMessage.value.actions_unregister.action_names_len =
				    static_cast<int>(names.size());
				neurosdk_error_e const error = SendMessage(&unregisterMessage);
				if (error != NeuroSDK_None) {
					LogError("Send queued unregister failed", error);
					ReportAbandoned(pending, index);
					return;
				}
			}

			if (!entry.actions.empty()) {
				std::vector<neurosdk_action_t> actions;
				actions.reserve(entry.actions.size());
				for (RegisteredAction const &action : entry.actions) {
					actions.push_back({const_cast<char *>(action.name.c_str()),
					                   const_cast<char *>(action.description.c_str()),
					                   const_cast<char *>(action.jsonSchema.c_str())});
				}
				neurosdk_message_t registerMessage = {};
				registerMessage.kind = NeuroSDK_MessageKind_ActionsRegister;
				registerMessage.value.actions_register.actions = actions.data();
				registerMessage.value.actions_register.actions_len =
				    static_cast<int>(actions.size());
				neurosdk_error_e const error = SendMessage(&registerMessage);
				if (error != NeuroSDK_None) {
					LogError("Send queued register failed", error);
					ReportAbandoned(pending, index);
					return;
				}
			}
			continue;
		}

		// Send non-batched messages immediately
		neurosdk_message_t message = {};
		message.kind = entry.kind;
		neurosdk_error_e error = NeuroSDK_None;
		// Lives out here, not inside the case that fills it: the message holds
		// bare pointers into this buffer and is sent after the switch, so a
		// vector scoped to the case would already be freed by then.
		std::vector<char *> forceNames;

		switch (entry.kind) {
			case NeuroSDK_MessageKind_ActionResult:
				message.value.action_result.id = const_cast<char *>(entry.id.c_str());
				message.value.action_result.success = entry.success;
				message.value.action_result.message =
				    const_cast<char *>(entry.message.c_str());
				break;

			case NeuroSDK_MessageKind_ActionsForce: {
				forceNames.reserve(entry.actionNames.size());
				for (std::string const &name : entry.actionNames) {
					forceNames.push_back(const_cast<char *>(name.c_str()));
				}
				message.value.actions_force.state =
				    const_cast<char *>(entry.state.c_str());
				message.value.actions_force.query =
				    const_cast<char *>(entry.query.c_str());
				message.value.actions_force.ephemeral_context = entry.ephemeral;
				message.value.actions_force.action_names = forceNames.data();
				message.value.actions_force.action_names_len =
				    static_cast<int>(forceNames.size());
				message.value.actions_force.priority = NeuroSDK_Priority_Medium;
				break;
			}

			default:
				continue;
		}

		error = SendMessage(&message);
		if (error != NeuroSDK_None) {
			LogError("Send queued message failed", error);
			ReportAbandoned(pending, index);
			return;
		}
		if (entry.kind == NeuroSDK_MessageKind_ActionResult) {
			ActionTimingResultSent(entry.id);
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
		{
			std::lock_guard<std::mutex> lock(g_UnansweredActionsMutex);
			g_UnansweredActions.insert(id);
		}
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
					ActionTimingStart(id, name);
					DispatchAction(id, name, data);
					break;
				}
				case NeuroSDK_MessageKind_ActionsReregisterAll:
					Log("[Neuro] Re-registering current actions\n");
					{
						std::lock_guard<std::mutex> lock(g_ActionsMutex);
						QueueActionsSnapshot({g_RegisteredActions, {}});
					}
					break;
				case NeuroSDK_MessageKind_StartupAcknowledgement:
					// Nothing to gate on it; it names the character the socket was
					// attached to, which is worth seeing in the log.
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

// What the link took with it, in the two shapes it can take: answers written
// but never sent, and actions the game is still working on. Said out loud
// because both look the same from outside -- Neuro asked and nothing came back.
void ClearOutboundQueues() {
	std::size_t results = 0;
	std::size_t context = 0;
	std::size_t unanswered = 0;
	{
		std::lock_guard<std::mutex> lock(g_OutboundContextMutex);
		context = g_OutboundContext.size();
		g_OutboundContext.clear();
	}
	{
		std::lock_guard<std::mutex> lock(g_OutboundMessagesMutex);
		for (OutboundMessage const &message : g_OutboundMessages) {
			if (message.kind == NeuroSDK_MessageKind_ActionResult) {
				results += 1;
			}
		}
		g_OutboundMessages.clear();
	}
	{
		std::lock_guard<std::mutex> lock(g_UnansweredActionsMutex);
		unanswered = g_UnansweredActions.size();
		g_UnansweredActions.clear();
	}
	if (results != 0 || unanswered != 0 || context != 0) {
		char line[160];
		sprintf_s(line,
		          "[Neuro] The link took %zu unsent result(s), %zu action(s) "
		          "still being answered and %zu context message(s)\n",
		          results, unanswered, context);
		Log(line);
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
	// The SDK waits on the socket for this long, so an arriving message wakes it
	// at once -- but everything we queue goes out only after that wait returns.
	// At 100 ms an action result therefore sat for up to a tenth of a second
	// after the work was already done, which was most of the delay Neuro's
	// client measured on our answers.
	//
	// Ten is a wait, not a spin: with nothing to read the thread wakes a hundred
	// times a second to check its outbound queue and sleeps again.
	description.poll_ms = 10;
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
	if (error != NeuroSDK_None) {
		LogError("Session initialization failed", error);
		DestroyContext();
		return false;
	}

	// Told before anything is sent, so whoever fills the set and answers the
	// actions starts this session having forgotten the last one. Held under the
	// handler mutex only long enough to read the pointer: the callback itself
	// runs outside it, because it reaches back into the caller's own state.
	NeuroSessionHandler sessionHandler = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_ActionHandlerMutex);
		sessionHandler = g_SessionHandler;
	}
	if (sessionHandler) {
		sessionHandler();
	}

	// Queue the current action snapshot to be sent by the poll thread.
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		QueueActionsSnapshot({g_RegisteredActions, {}});
	}

	g_NeuroConnected.store(true, std::memory_order_release);
	// Ready as soon as the startup message and the current set are out. The
	// acknowledgement is optional -- the specification says Neuro "may" send it
	// in response to startup -- so waiting for it before talking leaves the game
	// mute against a server that never sends one: actions arrive and no result,
	// context or force ever goes back.
	g_NeuroReady.store(true, std::memory_order_release);
	Log("[Neuro] Connected\n");
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
			if (error == NeuroSDK_None) {
				FlushOutboundMessages();
			}
		}

		DestroyContext();
		ClearOutboundQueues();
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
	ClearOutboundQueues();
	g_NeuroConnected.store(false, std::memory_order_release);
	g_NeuroReady.store(false, std::memory_order_release);
	Log("[Neuro] Poll thread stopped\n");
}
}  // namespace

void NeuroSetActionHandler(NeuroActionHandler handler) {
	std::lock_guard<std::mutex> lock(g_ActionHandlerMutex);
	g_ActionHandler = handler;
}

void NeuroSetSessionHandler(NeuroSessionHandler handler) {
	std::lock_guard<std::mutex> lock(g_ActionHandlerMutex);
	g_SessionHandler = handler;
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

	// The config first: naming an address in a file is a decision, while a
	// variable left in a shell is usually an accident from an earlier run.
	std::string url = GetPluginConfig().WsUrl;
	if (url.empty()) {
		char const *wsUrl = std::getenv("NEURO_WS_URL");
		url = (wsUrl && *wsUrl) ? wsUrl : "ws://localhost:8000";
	}
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
	bool identical = false;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		// The whole set is sent on every change, and the previous names are
		// unregistered first so the server sees an exact replacement.
		//
		// Sending the whole set is what carries the order: it is built to mirror
		// the screen, and a client that renders the register payload can only
		// show that order if every entry is in it. Registering a name that is
		// already registered is legitimate and replaces its definition, so the
		// This also preserves the order represented by the new register payload.
		identical = g_RegisteredActions.size() == static_cast<size_t>(actionCount);
		for (int index = 0; identical && index < actionCount; ++index) {
			RegisteredAction const &existing = g_RegisteredActions[index];
			char const *const name = actions[index].name;
			char const *const description = actions[index].description;
			char const *const schema = actions[index].jsonSchema;
			identical = existing.name == (name ? name : "") &&
			            existing.description == (description ? description : "") &&
			            existing.jsonSchema == (schema ? schema : "{}");
		}
		// Nothing to say when the set is the same one, down to the order. This is
		// the common case at 5 Hz, and re-sending it would be traffic for
		// nothing.
		if (identical) {
			return true;
		}
		for (RegisteredAction const &existing : g_RegisteredActions) {
			droppedNames.push_back(existing.name);
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

	std::vector<RegisteredAction> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		snapshot = g_RegisteredActions;
	}
	QueueActionsSnapshot({snapshot, droppedNames});
	return true;
}

bool NeuroActionIsRegistered(char const *name) {
	if (!name || !*name) {
		return false;
	}
	std::lock_guard<std::mutex> lock(g_ActionsMutex);
	for (RegisteredAction const &action : g_RegisteredActions) {
		if (action.name == name) {
			return true;
		}
	}
	return false;
}

bool NeuroRegisterAction(char const *name,
                         char const *description,
                         char const *jsonSchema) {
	if (!name || !*name || !description) {
		return false;
	}
	std::vector<std::string> previousNames;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		for (RegisteredAction const &action : g_RegisteredActions) {
			previousNames.push_back(action.name);
		}
		g_RegisteredActions.push_back(
		    {name, description, jsonSchema ? jsonSchema : "{}"});
	}
	if (!NeuroIsConnected()) {
		return true;
	}
	std::vector<RegisteredAction> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		snapshot = g_RegisteredActions;
	}
	QueueActionsSnapshot({snapshot, previousNames});
	return true;
}

bool NeuroUnregisterAction(char const *name) {
	if (!name || !*name) {
		return false;
	}
	std::vector<std::string> previousNames;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		auto &actions = g_RegisteredActions;
		for (RegisteredAction const &action : actions) {
			previousNames.push_back(action.name);
		}
		actions.erase(std::remove_if(actions.begin(), actions.end(),
		                             [&](RegisteredAction const &action) {
			                             return action.name == name;
		                             }),
		              actions.end());
	}
	if (!NeuroIsConnected()) {
		return true;
	}
	std::vector<RegisteredAction> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		snapshot = g_RegisteredActions;
	}
	QueueActionsSnapshot({snapshot, previousNames});
	return true;
}

bool NeuroReregisterActions() {
	if (!NeuroIsConnected()) {
		return true;
	}
	std::vector<RegisteredAction> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		snapshot = g_RegisteredActions;
	}
	QueueActionsSnapshot({snapshot, {}});
	return true;
}

// Whether the last result sent said the action succeeded. Read by the router to
// decide whether a forced action has actually been carried out: the server
// keeps a force alive through a failed answer and asks again, so retiring ours
// on the failure would let the next force go out while the server still held
// the first -- which is exactly what the reference server refuses.
std::atomic<bool> g_LastActionResultSucceeded(false);

bool NeuroSendActionResult(char const *id, bool success, char const *message) {
	g_LastActionResultSucceeded.store(success, std::memory_order_release);
	// Nothing can be sent while the link is down, and the action is lost with
	// it: the caller has no way to retry and the server is left waiting until
	// it gives up. Said out loud, because a result that never arrives is
	// otherwise indistinguishable from a handler that forgot to answer.
	if (!id || !NeuroIsReady()) {
		if (id) {
			Log(("[Neuro] Not connected, dropped the result for " + std::string(id) +
			     "\n")
			        .c_str());
		}
		return false;
	}
	OutboundMessage msg = {};
	msg.kind = NeuroSDK_MessageKind_ActionResult;
	msg.id = id;
	msg.success = success;
	msg.message = message ? message : "";
	{
		std::lock_guard<std::mutex> lock(g_OutboundMessagesMutex);
		MakeRoomForOutboundMessage();
		g_OutboundMessages.push_back(msg);
	}
	{
		std::lock_guard<std::mutex> lock(g_UnansweredActionsMutex);
		g_UnansweredActions.erase(id);
	}
	ActionTimingResultQueued(id);
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
	OutboundMessage msg = {};
	msg.kind = NeuroSDK_MessageKind_ActionsForce;
	msg.state = state ? state : "";
	msg.query = query ? query : "";
	msg.ephemeral = ephemeral;
	for (int i = 0; i < actionCount; ++i) {
		msg.actionNames.push_back(actionNames[i]);
	}
	{
		std::lock_guard<std::mutex> lock(g_OutboundMessagesMutex);
		MakeRoomForOutboundMessage();
		g_OutboundMessages.push_back(msg);
	}
	return true;
}
