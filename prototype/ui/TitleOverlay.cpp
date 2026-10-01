#include "TitleOverlay.h"

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
std::string g_Picture;
// The game was reached since the title was last shown: the way back may pass
// through a menu (settings -> return to title), and still counts as from the
// game.
bool g_SawGame = false;
bool g_ReturnHint = false;

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
		// Leaving the title for a menu, the picture waits on the pages so it is
		// back before the title fades in again. Reaching the game drops it,
		// whichever way the game was reached: a save loaded from the title's
		// load screen passes through a menu first, and a picture left waiting
		// there would come back at once instead of being shown anew.
		if (!state.empty() && GetPluginConfig().TitleOverlay) {
			if (state == "game") {
				ExecuteTJSAsync(
				    L"typeof global.neuroTitleOverlay == 'Object' && "
				    L"global.neuroTitleOverlay.leave()",
				    TJSExecMode::Expression);
			} else if (g_LastState == "title") {
				ExecuteTJSAsync(
				    L"typeof global.neuroTitleOverlay == 'Object' && "
				    L"global.neuroTitleOverlay.watch()",
				    TJSExecMode::Expression);
			}
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
	if (!ready || !GetPluginConfig().TitleOverlay) {
		return;
	}
	// Evil only when the server says so; no session or any other character is
	// Neuro.
	std::string const picture =
	    NeuroCharacterId() == "evil" ? "overlay_e" : "overlay_n";
	std::wstring const widePicture(picture.begin(), picture.end());
	if (g_WasReady) {
		// A reconnect to the other twin while the title is up changes the
		// picture in place.
		if (picture != g_Picture) {
			g_Picture = picture;
			ExecuteTJSAsync((L"typeof global.neuroTitleOverlay == 'Object' && "
			                 L"global.neuroTitleOverlay.setPicture('" +
			                 widePicture + L"')")
			                    .c_str(),
			                TJSExecMode::Expression);
			Log(("[Overlay] Picture changed to " + picture + "\n").c_str());
		}
		return;
	}
	g_WasReady = true;
	g_Picture = picture;
	bool const instant = !g_LastOther.empty() && !g_SawGame;
	g_ReturnHint = g_SawGame;
	g_SawGame = false;
	std::wstring code =
	    L"typeof global.neuroTitleOverlay == 'Object' && "
	    L"global.neuroTitleOverlay.arm('";
	code += widePicture;
	code += instant ? L"', true)" : L"')";
	ExecuteTJSAsync(code.c_str(), TJSExecMode::Expression);
	Log(("[Overlay] Armed: " + g_Picture +
	     (instant ? ", at once after '" + g_LastOther + "'" : ", animated") +
	     "\n")
	        .c_str());
}

bool TakeReturnHint() {
	bool const hint = g_ReturnHint;
	g_ReturnHint = false;
	return hint;
}

}  // namespace TitleOverlay
