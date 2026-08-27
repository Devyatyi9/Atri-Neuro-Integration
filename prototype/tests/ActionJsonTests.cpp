#include "ActionJson.h"

#include <string>

#include "TestMain.h"

namespace {

// The payloads are written out as text rather than built from values on
// purpose. The cases worth catching -- a bare fraction where an integer
// belongs, two signs, a literal past the end of int -- are the ones a JSON
// encoder would never produce and a hand-rolled reader gets wrong.
void ExpectInteger(char const *json, char const *key, int expected) {
	int out = -999;
	bool const ok = ParseJsonInteger(json, key, out);
	ReportCheck(ok && out == expected, __FILE__, __LINE__, json);
}

void ExpectIntegerRefused(char const *json, char const *key) {
	int out = -999;
	bool const ok = ParseJsonInteger(json, key, out);
	ReportCheck(!ok && out == -999, __FILE__, __LINE__, json);
}

void ExpectBoolean(char const *json, char const *key, int expected) {
	int out = -999;
	bool const ok = ParseJsonBoolean(json, key, out);
	ReportCheck(ok && out == expected, __FILE__, __LINE__, json);
}

void ExpectBooleanRefused(char const *json, char const *key) {
	int out = -999;
	bool const ok = ParseJsonBoolean(json, key, out);
	ReportCheck(!ok && out == -999, __FILE__, __LINE__, json);
}

}  // namespace

void RunActionJsonTests() {
	ExpectInteger(R"({"slot": 3})", "\"slot\"", 3);
	ExpectInteger(R"({"slot":10})", "\"slot\"", 10);
	ExpectInteger(R"({"slot": -2 })", "\"slot\"", -2);
	ExpectInteger(R"({"slot":0})", "\"slot\"", 0);
	ExpectInteger(R"({"page": 4, "slot": 5})", "\"slot\"", 5);
	ExpectInteger("{\"slot\":\n\t6\n}", "\"slot\"", 6);

	// Every one of these used to come back as a number inside the range the
	// handlers accept, which is how a fraction became a real page turn.
	ExpectIntegerRefused(R"({"slot": 2.9})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": 1-2})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": --3})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": 99999999999})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": 1e3})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": "3"})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": true})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": null})", "\"slot\"");
	ExpectIntegerRefused(R"({"slot": })", "\"slot\"");
	ExpectIntegerRefused(R"({"other": 7})", "\"slot\"");
	ExpectIntegerRefused("", "\"slot\"");

	ExpectBoolean(R"({"confirm": true})", "\"confirm\"", 1);
	ExpectBoolean(R"({"confirm":false})", "\"confirm\"", 0);
	ExpectBoolean(R"({"confirm": true })", "\"confirm\"", 1);
	ExpectBoolean(R"({"confirm": false, "slot": 2})", "\"confirm\"", 0);

	ExpectBooleanRefused(R"({"confirm": truthy})", "\"confirm\"");
	ExpectBooleanRefused(R"({"confirm": falsey})", "\"confirm\"");
	ExpectBooleanRefused(R"({"confirm": 1})", "\"confirm\"");
	ExpectBooleanRefused(R"({"confirm": "true"})", "\"confirm\"");
	ExpectBooleanRefused(R"({"other": true})", "\"confirm\"");

	// An action whose schema has no properties arrives with no data at all,
	// which reaches the plugin as an empty string.
	CHECK(IsEmptyJsonObject(""));
	CHECK(IsEmptyJsonObject("   "));
	CHECK(IsEmptyJsonObject("{}"));
	CHECK(IsEmptyJsonObject("  {}  "));
	CHECK(!IsEmptyJsonObject(R"({"slot": 1})"));
	CHECK(!IsEmptyJsonObject("[]"));
}
