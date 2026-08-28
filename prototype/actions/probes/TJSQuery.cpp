#include "TJSQuery.h"

#include <windows.h>

#include <string>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "Logging.h"
#include "TJSExecutor.h"
#include "UtfConversion.h"

bool QueryString(wchar_t const *expr, std::string &out, char const *tag) {
	// Both of these run on the Neuro polling thread, so the answer arrives as
	// text: the conversion from the engine's own string happens on the game's
	// main thread, inside the call. Nothing engine-owned comes back here.
	if (!ExecuteTJSSync(expr, &out, 5000, TJSExecMode::Expression)) {
		Log(("[Action] " + std::string(tag) + " probe failed\n").c_str());
		return false;
	}
	return true;
}

bool QueryBool(wchar_t const *expr, bool &out, char const *tag) {
	std::string answer;
	if (!ExecuteTJSSync(expr, &answer, 5000, TJSExecMode::Expression)) {
		Log(("[Action] " + std::string(tag) + " probe failed\n").c_str());
		return false;
	}
	// The probes answer with 0 or 1; anything else, an empty answer included, is
	// read as false rather than guessed at.
	out = answer == "1";
	return true;
}
