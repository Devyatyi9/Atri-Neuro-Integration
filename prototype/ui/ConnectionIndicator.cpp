#include "ConnectionIndicator.h"

#include <windows.h>

#include <string>

#include "Logging.h"
#include "PluginConfig.h"
#include "TJSExecutor.h"

namespace {

// Long enough to read as a blink rather than a flicker, and slow enough that
// the upkeep it rides on carries at most a couple of calls a second.
constexpr DWORD kBlinkMs = 600;

// A red dot, top left: away from the quick menu bar, which slides in over the
// right side, and from the text window along the bottom. Above everything,
// because the dot is worth nothing if a menu can cover it.
wchar_t const *const kShowDot =
    L"(function(){"
    // Nothing is shown over the splash screens: the state is empty until the
    // game reaches a screen of its own, and a dot blinking over the logos says
    // only that the game has not finished starting.
    L" if (typeof SystemHook == 'undefined' || SystemHook.currentState == '')"
    L" return 0;"
    L" var d = typeof global.__neuroLinkDot != 'undefined'"
    L" ? global.__neuroLinkDot : void;"
    L" if (d === void || !isvalid d) {"
    L" d = new Layer(kag, kag.uibase);"
    L" d.setSize(10, 10); d.setPos(12, 12);"
    L" d.face = dfAlpha; d.fillRect(0, 0, 10, 10, 0x00000000);"
    // A layer has no circle fill, so the disc is drawn pixel by pixel: a
    // hundred one-pixel fills, once. Every pixel is fully opaque and the ones
    // outside the circle are left out, which at this size reads as a crisp dot
    // rather than a smudge -- a soft edge over ten pixels is mostly edge.
    L" for (var y = 0; y < 10; y++) { for (var x = 0; x < 10; x++) {"
    L" var dx = x - 4.5, dy = y - 4.5;"
    L" if (Math.sqrt(dx * dx + dy * dy) > 4.8) continue;"
    L" d.fillRect(x, y, 1, 1, 0xFFE02020); } }"
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
	if (!GetPluginConfig().LinkIndicator) {
		// Switched off mid-run it would leave the dot on screen, so the one
		// that is already there is put away first.
		if (g_showing) {
			g_showing = false;
			g_lit = false;
			Draw(false);
		}
		return;
	}
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
