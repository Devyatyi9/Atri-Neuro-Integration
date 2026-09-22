#include "ActionCommon.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "ActionJson.h"
#include "ActionProbes.h"
#include "NeuroClient.h"

bool WaitForScreen(std::function<bool()> const &ready, DWORD timeoutMs) {
	// Asked once before any waiting: the screen is sometimes already there, and
	// that case should cost one crossing rather than one interval.
	if (ready()) {
		return true;
	}
	DWORD const started = GetTickCount();
	while (GetTickCount() - started < timeoutMs) {
		Sleep(kScreenPollMs);
		if (ready()) {
			return true;
		}
	}
	return false;
}

void AnswerPress(std::string const &id,
                 bool ok,
                 char const *okMessage,
                 char const *failMessage) {
	if (ok) {
		NeuroSendActionResult(id.c_str(), true, okMessage);
		return;
	}
	NeuroSendActionResult(id.c_str(), false,
	                      LastPressWasBusy() ? kPressStillRunning : failMessage);
}

void HandleScreenButtonAction(std::string const &id,
                              std::string const &data,
                              char const *action,
                              char const *state,
                              char const *button,
                              char const *okMessage,
                              char const *failMessage) {
	if (!IsEmptyJsonObject(data)) {
		NeuroSendActionResult(
		    id.c_str(), false,
		    (std::string(action) + " does not accept arguments.").c_str());
		return;
	}
	std::string const tag(action);
	bool const ok = ProbeStateIs(state, (tag + " state").c_str()) &&
	                ProbeScreenButtonReady(button, (tag + " button").c_str()) &&
	                DispatchScreenButton(button, (tag + " dispatch").c_str());
	AnswerPress(id, ok, okMessage, failMessage);
}

namespace {
// The last thing a screen said about itself. Not cleared on reconnect: that is
// the case this exists for.
std::string g_LastScreenAnnouncement;
}  // namespace

bool SendScreenAnnouncement(std::string const &text) {
	if (text.empty() || text == g_LastScreenAnnouncement) {
		return false;
	}
	g_LastScreenAnnouncement = text;
	return NeuroSendContext(text.c_str(), false);
}

void ForgetScreenAnnouncement() {
	g_LastScreenAnnouncement.clear();
}

int ParseReadLanguage(std::string const &data) {
	return data.find("japanese") == std::string::npos ? -1 : 0;
}

std::string BoundedIntSchema(char const *field, int maximum) {
	return std::string("{\"type\":\"object\",\"properties\":{\"") + field +
	       "\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":" +
	       std::to_string(maximum) + "}},\"required\":[\"" + field +
	       "\"],\"additionalProperties\":false}";
}

std::string RangeIntSchema(char const *field, int minimum, int maximum) {
	return std::string("{\"type\":\"object\",\"properties\":{\"") + field +
	       "\":{\"type\":\"integer\",\"minimum\":" + std::to_string(minimum) +
	       ",\"maximum\":" + std::to_string(maximum) + "}},\"required\":[\"" +
	       field + "\"],\"additionalProperties\":false}";
}

std::string EnumStringSchema(char const *field,
                             std::vector<char const *> const &values) {
	std::string schema = std::string("{\"type\":\"object\",\"properties\":{\"") +
	                     field + "\":{\"type\":\"string\",\"enum\":[";
	for (size_t index = 0; index < values.size(); ++index) {
		if (index != 0) {
			schema += ",";
		}
		schema += std::string("\"") + values[index] + "\"";
	}
	schema += "]}},\"required\":[\"";
	schema += field;
	schema += "\"],\"additionalProperties\":false}";
	return schema;
}
