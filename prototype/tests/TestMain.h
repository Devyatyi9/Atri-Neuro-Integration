#pragma once

// A test runner is not worth a dependency here. Each case is one line, a
// failure prints what was expected against what happened, and the count of
// failures is the exit code.
//
// Kept to the same language level as the rest of the project rather than
// raising the bar for this target alone.

extern int g_TestFailures;
extern int g_TestChecks;

void ReportCheck(bool passed,
                 char const *file,
                 int line,
                 char const *description);

#define CHECK(condition) \
	ReportCheck((condition), __FILE__, __LINE__, #condition)
