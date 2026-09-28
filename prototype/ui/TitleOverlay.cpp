#include "TitleOverlay.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "Logging.h"
#include "NeuroClient.h"
#include "PluginConfig.h"
#include "TJSExecutor.h"

namespace {

bool g_WasReady = false;
// The last state other than the title, which decides how the picture returns.
std::string g_LastOther;
std::string g_LastState;
// The game was reached since the title was last shown: the way back may pass
// through a menu (settings -> return to title), and still counts as from the
// game.
bool g_SawGame = false;

}  // namespace

namespace TitleOverlay {

void Install() {
	if (!GetPluginConfig().TitleOverlay) {
		return;
	}
	// execStorage rather than Neuro.execFile: the script declares a class, which
	// an expression cannot.
	ExecuteTJSAsync(
	    L"Scripts.execStorage(System.exePath + "
	    L"'neuro-assets/install/title_overlay.tjs')",
	    TJSExecMode::Expression);
}

void NoteScreen(std::string const &state, bool ready) {
	if (state != g_LastState) {
		// Leaving the title: for the game the picture is dropped, for a menu it
		// waits on the pages so it is back before the title fades in again.
		if (g_LastState == "title" && !state.empty() &&
		    GetPluginConfig().TitleOverlay) {
			ExecuteTJSAsync(state == "game"
			                    ? L"typeof global.neuroTitleOverlay == 'Object' && "
			                      L"global.neuroTitleOverlay.leave()"
			                    : L"typeof global.neuroTitleOverlay == 'Object' && "
			                      L"global.neuroTitleOverlay.watch()",
			                TJSExecMode::Expression);
		}
		g_LastState = state;
	}
	if (state == "game") {
		g_SawGame = true;
	}
	if (state != "title") {
		g_LastOther = state;
		g_WasReady = false;
		return;
	}
	if (!ready || g_WasReady || !GetPluginConfig().TitleOverlay) {
		return;
	}
	g_WasReady = true;
	// Evil only when the server says so; no session or any other character is
	// Neuro.
	std::string id = NeuroCharacterId();
	std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	bool const evil = id.find("evil") != std::string::npos;
	bool const instant = !g_LastOther.empty() && !g_SawGame;
	g_SawGame = false;
	std::wstring code =
	    L"typeof global.neuroTitleOverlay == 'Object' && "
	    L"global.neuroTitleOverlay.arm('";
	code += evil ? L"overlay_e" : L"overlay_n";
	code += instant ? L"', true)" : L"')";
	ExecuteTJSAsync(code.c_str(), TJSExecMode::Expression);
	Log(("[Overlay] Armed: " + std::string(evil ? "evil" : "neuro") +
	     (instant ? ", at once after '" + g_LastOther + "'" : ", animated") +
	     "\n")
	        .c_str());
}

}  // namespace TitleOverlay
