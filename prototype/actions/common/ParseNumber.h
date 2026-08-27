#pragma once

#include <string>

// Checked integer parsing, kept apart from everything that needs the game.
//
// Both callers -- the probe answers coming back from TJS and the action data
// coming in from Neuro -- have the same problem: zero is a legitimate value in
// every place they use, so a conversion that quietly yields zero for malformed
// input is indistinguishable from an honest zero. These say so instead.
//
// This header deliberately depends on nothing but the standard library, so the
// rules can be pinned down by tests that do not need the game running.

// The whole text has to be one integer: optional surrounding blanks, an
// optional minus, digits, and nothing else. Trailing text, a fraction, an
// exponent, or a value outside `int` are all refused.
bool ParseWholeInt(std::string const &text, int &out);

// The same, for text that is already isolated as a token by its caller.
// Leading and trailing blanks are not accepted here.
bool ParseIntToken(std::string const &token, int &out);
