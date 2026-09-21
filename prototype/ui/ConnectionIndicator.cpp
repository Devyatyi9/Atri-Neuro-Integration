#include "ConnectionIndicator.h"

#include <windows.h>

#include <string>

#include "Logging.h"
#include "TJSExecutor.h"

namespace {

// Long enough to read as a blink rather than a flicker, and slow enough that
// the upkeep it rides on carries at most a couple of calls a second.
constexpr DWORD kBlinkMs = 600;

// Top left, away from the quick menu bar, which slides in over the right side,
// and from the text window along the bottom. Above everything: the dot is worth
// nothing if a menu can cover it.
wchar_t const *const kShowDot =
    L"(function(){"
    L" var d = typeof global.__neuroLinkDot != 'undefined'"
    L" ? global.__neuroLinkDot : void;"
    L" if (d === void || !isvalid d) {"
    L" d = new Layer(kag, kag.uibase);"
    L" d.setSize(14, 14); d.setPos(12, 12);"
    L" d.face = dfAlpha; d.fillRect(0, 0, 14, 14, 0xFFE02020);"
    L" d.absolute = 2000000;"
    // Clicks have to pass through it, and a mask hit test with the threshold
    // above opaque is what lets them.
    L" d.hitType = htMask; d.hitThreshold = 256;"
    L" global.__neuroLinkDot = d; }"
    L" d.visible = true; return 1; })()";

// Hiding never builds the layer: with the link up there is nothing to show, and
// a game that never lost it should not carry our layer at all.
wchar_t const *const kHideDot =
    L"(function(){"
    L" var d = typeof global.__neuroLinkDot != 'undefined'"
    L" ? global.__neuroLinkDot : void;"
    L" if (d === void || !isvalid d) return 0;"
    L" d.visible = false; return 1; })()";

bool g_showing = false;
bool g_lit = false;
DWORD g_lastToggle = 0;

void Draw(bool lit) {
	std::string answer;
	if (!ExecuteTJSSync(lit ? kShowDot : kHideDot, &answer, 5000,
	                    TJSExecMode::Expression)) {
		// Said once per state change, not once per blink: a failure here repeats
		// twice a second for as long as the link is down.
		static bool complained = false;
		if (!complained) {
			complained = true;
			Log("[Link] could not draw the connection indicator\n");
		}
	}
}

}  // namespace

namespace ConnectionIndicator {

void Update(bool connected) {
	if (connected) {
		if (g_showing) {
			g_showing = false;
			g_lit = false;
			Draw(false);
		}
		return;
	}

	DWORD const now = GetTickCount();
	if (!g_showing) {
		g_showing = true;
		g_lit = false;
		// Lit on the next line rather than here, so the dot appears at once
		// instead of a blink later.
		g_lastToggle = now - kBlinkMs;
	}
	if (now - g_lastToggle < kBlinkMs) {
		return;
	}
	g_lastToggle = now;
	g_lit = !g_lit;
	Draw(g_lit);
}

}  // namespace ConnectionIndicator
