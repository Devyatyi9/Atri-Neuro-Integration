#include "TestMain.h"

#include <cstdio>

int g_TestFailures = 0;
int g_TestChecks = 0;

void ReportCheck(bool passed,
                 char const *file,
                 int line,
                 char const *description) {
	++g_TestChecks;
	if (passed) {
		return;
	}
	++g_TestFailures;
	printf("FAIL %s:%d  %s\n", file, line, description);
}

void RunParseNumberTests();
void RunActionJsonTests();
void RunSlotPayloadTests();
void RunPluginConfigTests();
void RunContextPayloadTests();
void RunVisibleCharacterTests();

int main() {
	RunParseNumberTests();
	RunActionJsonTests();
	RunSlotPayloadTests();
	RunPluginConfigTests();
	RunContextPayloadTests();
	RunVisibleCharacterTests();

	printf("%d checks, %d failed\n", g_TestChecks, g_TestFailures);
	return g_TestFailures == 0 ? 0 : 1;
}
