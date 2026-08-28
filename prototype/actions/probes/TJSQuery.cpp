#include "TJSQuery.h"

#include <windows.h>

#include <string>

#pragma warning(push)
#pragma warning(disable : 26495)
#include "tp_stub.h"
#pragma warning(pop)

#include "Logging.h"
#include "ParseNumber.h"
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
	// An empty answer is what a probe gives when the thing it asks about is not
	// there, and that reads as false.
	if (answer.empty()) {
		out = false;
		return true;
	}
	// Otherwise it is a number, and any non-zero is true. Every probe today
	// answers 0 or 1, but comparing against "1" alone would read a future 2 as
	// false -- quietly, which is the one outcome worth ruling out.
	int value = 0;
	if (!ParseWholeInt(answer, value)) {
		Log(("[Action] " + std::string(tag) + " expected 0 or 1, got '" + answer +
		     "'\n")
		        .c_str());
		return false;
	}
	out = value != 0;
	return true;
}
