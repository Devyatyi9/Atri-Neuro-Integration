#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "NeuroClient.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../include/neurosdk.h"
#include "Logging.h"

namespace {
neurosdk_context_t g_NeuroContext = nullptr;
std::atomic<bool> g_NeuroConnected(false);
std::atomic<bool> g_NeuroPollThreadRunning(false);
std::thread g_NeuroPollThread;
std::string g_NeuroGameName = "ATRI-MyDearMoments";
std::string g_NeuroURL = "ws://localhost:8000";

std::vector<std::string> g_RegisteredActions;
std::mutex g_ActionsMutex;

NeuroActionHandler g_ActionHandler = nullptr;
std::mutex g_ActionHandlerMutex;

void NeuroLogCallback(neurosdk_severity_e severity,
                      char *message,
                      void *userData) {
	(void)userData;
	char const *severityStr = "???";
	switch (severity) {
		case NeuroSDK_Severity_Debug:
			severityStr = "DEBUG";
			break;
		case NeuroSDK_Severity_Info:
			severityStr = "INFO";
			break;
		case NeuroSDK_Severity_Warn:
			severityStr = "WARN";
			break;
		case NeuroSDK_Severity_Error:
			severityStr = "ERROR";
			break;
	}

	char buf[1024];
	sprintf_s(buf, "[NeuroSDK %s] %s\n", severityStr,
	          message ? message : "(null)");
	LogToFile(buf);
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
		char buf[512];
		sprintf_s(buf, "[Neuro] Action ignored (no handler): id=%s, name=%s\n",
		          id.c_str(), name.c_str());
		LogToFile(buf);
	}
}

void NeuroPollThread() {
	LogToFile("[Neuro] Poll thread started\n");

	while (g_NeuroPollThreadRunning.load() && g_NeuroConnected.load()) {
		if (!neurosdk_context_connected(&g_NeuroContext)) {
			LogToFile("[Neuro] Connection lost!\n");
			g_NeuroConnected.store(false);
			break;
		}

		neurosdk_message_t *messages = nullptr;
		int count = 0;
		neurosdk_error_e err =
		    neurosdk_context_poll(&g_NeuroContext, &messages, &count);

		if (err != NeuroSDK_None) {
			char buf[256];
			sprintf_s(buf, "[Neuro] Poll error: %s\n", neurosdk_error_string(err));
			LogToFile(buf);

			// Unknown commands do not terminate polling.
			if (err == NeuroSDK_UnknownCommand) {
				Sleep(100);
				continue;
			}
			break;
		}

		for (int i = 0; i < count; i++) {
			neurosdk_message_t *msg = &messages[i];
			if (msg->kind == NeuroSDK_MessageKind_Action) {
				std::string const id = msg->value.action.id ? msg->value.action.id : "";
				std::string const name =
				    msg->value.action.name ? msg->value.action.name : "";
				std::string const data =
				    msg->value.action.data ? msg->value.action.data : "";
				neurosdk_message_destroy(msg);
				DispatchAction(id, name, data);
			} else {
				neurosdk_message_destroy(msg);
			}
		}

		Sleep(50);
	}

	LogToFile("[Neuro] Poll thread stopped\n");
}
}  // namespace

void NeuroSetActionHandler(NeuroActionHandler handler) {
	std::lock_guard<std::mutex> lock(g_ActionHandlerMutex);
	g_ActionHandler = handler;
}

bool NeuroConnect(char const *url, char const *gameName) {
	if (g_NeuroConnected.load()) {
		LogToFile("[Neuro] Already connected\n");
		return true;
	}
	if (g_NeuroPollThread.joinable() || g_NeuroContext) {
		LogToFile("[Neuro] Disconnect the previous session before reconnecting\n");
		return false;
	}

	if (url)
		g_NeuroURL = url;
	if (gameName)
		g_NeuroGameName = gameName;

	char buf[512];
	sprintf_s(buf, "[Neuro] Connecting to %s as '%s'...\n", g_NeuroURL.c_str(),
	          g_NeuroGameName.c_str());
	LogToFile(buf);

	neurosdk_context_create_desc_t desc;
	memset(&desc, 0, sizeof(desc));
	desc.url = g_NeuroURL.c_str();
	desc.game_name = g_NeuroGameName.c_str();
	desc.poll_ms = 100;
	desc.user_data = nullptr;
	desc.callback_log = NeuroLogCallback;
	desc.flags = NeuroSDK_ContextCreateFlags_None;

	neurosdk_error_e err = neurosdk_context_create(&g_NeuroContext, &desc);
	if (err != NeuroSDK_None) {
		sprintf_s(buf, "[Neuro] Connection failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	g_NeuroConnected.store(true);
	LogToFile("[Neuro] Connected successfully!\n");

	g_NeuroPollThreadRunning.store(true);
	g_NeuroPollThread = std::thread(NeuroPollThread);
	return true;
}

void NeuroDisconnect() {
	bool const hadResources =
	    g_NeuroConnected.load() || g_NeuroPollThread.joinable() || g_NeuroContext;
	if (hadResources) {
		LogToFile("[Neuro] Disconnecting...\n");
	}

	g_NeuroPollThreadRunning.store(false);
	g_NeuroConnected.store(false);
	if (g_NeuroPollThread.joinable()) {
		g_NeuroPollThread.join();
	}

	if (g_NeuroContext) {
		neurosdk_context_destroy(&g_NeuroContext);
		g_NeuroContext = nullptr;
	}

	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		g_RegisteredActions.clear();
	}
	{
		std::lock_guard<std::mutex> lock(g_ActionHandlerMutex);
		g_ActionHandler = nullptr;
	}

	if (hadResources) {
		LogToFile("[Neuro] Disconnected\n");
	}
}

bool NeuroIsConnected() {
	return g_NeuroConnected.load();
}

bool NeuroSendContext(char const *message, bool silent) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot send context: not connected\n");
		return false;
	}

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_Context;
	msg.value.context.message = const_cast<char *>(message);
	msg.value.context.silent = silent;

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Send context failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	char buf[512];
	sprintf_s(buf, "[Neuro] Context sent: %s\n", message);
	LogToFile(buf);
	return true;
}

bool NeuroRegisterAction(char const *name,
                         char const *description,
                         char const *jsonSchema) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot register action: not connected\n");
		return false;
	}

	neurosdk_action_t action;
	action.name = const_cast<char *>(name);
	action.description = const_cast<char *>(description);
	action.json_schema = const_cast<char *>(jsonSchema ? jsonSchema : "{}");

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_ActionsRegister;
	msg.value.actions_register.actions = &action;
	msg.value.actions_register.actions_len = 1;

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Register action failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_ActionsMutex);
		g_RegisteredActions.push_back(name);
	}

	char buf[256];
	sprintf_s(buf, "[Neuro] Action registered: %s\n", name);
	LogToFile(buf);
	return true;
}

bool NeuroSendActionResult(char const *id, bool success, char const *message) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot send result: not connected\n");
		return false;
	}

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_ActionResult;
	msg.value.action_result.id = const_cast<char *>(id);
	msg.value.action_result.success = success;
	msg.value.action_result.message = const_cast<char *>(message ? message : "");

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Send result failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	char buf[256];
	sprintf_s(buf, "[Neuro] Result sent for %s: %s\n", id,
	          success ? "success" : "failure");
	LogToFile(buf);
	return true;
}

bool NeuroForceAction(char const *state,
                      char const *query,
                      char const **actionNames,
                      int actionCount,
                      bool ephemeral) {
	if (!g_NeuroConnected.load() || !g_NeuroContext) {
		LogToFile("[Neuro] Cannot force action: not connected\n");
		return false;
	}

	neurosdk_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.kind = NeuroSDK_MessageKind_ActionsForce;
	msg.value.actions_force.state = const_cast<char *>(state ? state : "");
	msg.value.actions_force.query = const_cast<char *>(query ? query : "");
	msg.value.actions_force.ephemeral_context = ephemeral;
	msg.value.actions_force.action_names = const_cast<char **>(actionNames);
	msg.value.actions_force.action_names_len = actionCount;
	msg.value.actions_force.priority = NeuroSDK_Priority_Medium;

	neurosdk_error_e err = neurosdk_context_send(&g_NeuroContext, &msg);
	if (err != NeuroSDK_None) {
		char buf[256];
		sprintf_s(buf, "[Neuro] Force action failed: %s\n",
		          neurosdk_error_string(err));
		LogToFile(buf);
		return false;
	}

	LogToFile("[Neuro] Force action sent\n");
	return true;
}
