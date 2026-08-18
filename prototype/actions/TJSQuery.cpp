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
	tTJSVariant result;
	if (!ExecuteTJSSync(expr, &result, 5000)) {
		Log(("[Action] " + std::string(tag) + " probe failed\n").c_str());
		return false;
	}
	ttstr value(result);
	if (!Utf16ToUtf8(value.c_str(), value.GetLen(), out)) {
		Log(("[Action] " + std::string(tag) + " conversion failed\n").c_str());
		return false;
	}
	return true;
}

bool QueryBool(wchar_t const *expr, bool &out, char const *tag) {
	tTJSVariant result;
	if (!ExecuteTJSSync(expr, &result, 5000)) {
		Log(("[Action] " + std::string(tag) + " probe failed\n").c_str());
		return false;
	}
	out = result.AsInteger() != 0;
	return true;
}