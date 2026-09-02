#include "ParseNumber.h"

#include <climits>

#include "TestMain.h"

namespace {

// Accepting a value and reporting the right one are two different claims, so
// both are checked every time: a parser that returns true but writes garbage is
// exactly the failure this whole unit exists to prevent.
void ExpectInt(char const *text, int expected) {
	int out = -999;
	bool const ok = ParseWholeInt(text, out);
	ReportCheck(ok && out == expected, __FILE__, __LINE__, text);
}

void ExpectRefused(char const *text) {
	int out = -999;
	bool const ok = ParseWholeInt(text, out);
	// The output must be left alone as well: a caller that ignores the return
	// value should not find a number that was never there.
	ReportCheck(!ok && out == -999, __FILE__, __LINE__, text);
}

}  // namespace

void RunParseNumberTests() {
	ExpectInt("0", 0);
	ExpectInt("7", 7);
	ExpectInt("-7", -7);
	ExpectInt("  12  ", 12);
	ExpectInt("\t3\t", 3);
	ExpectInt("2147483647", INT_MAX);
	ExpectInt("-2147483648", INT_MIN);

	ExpectRefused("");
	ExpectRefused("   ");
	ExpectRefused("abc");
	ExpectRefused("12abc");
	ExpectRefused("12 34");
	ExpectRefused("1.5");
	ExpectRefused("1e3");
	ExpectRefused("--3");
	ExpectRefused("+3");
	ExpectRefused("0x10");
	ExpectRefused("2147483648");
	ExpectRefused("-2147483649");
	ExpectRefused("99999999999");
	ExpectRefused("1|2");

	// The token form is what the JSON reader hands in: already isolated, so a
	// blank inside it means the caller cut it wrong.
	int out = 0;
	CHECK(ParseIntToken("42", out) && out == 42);
	CHECK(!ParseIntToken(" 42", out));
	CHECK(!ParseIntToken("42 ", out));
	CHECK(!ParseIntToken("", out));
}
