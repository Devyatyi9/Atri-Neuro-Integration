#pragma once

#include <string>

namespace TitleOverlay {

// Loads neuro-assets/install/title_overlay.tjs once TJS is ready.
void Install();

// Call from the periodic upkeep with the screen state and, on the title,
// whether it is ready. The first ready reading after being elsewhere arms the
// show: animated after startup or the game, at once after a menu, which leaves
// the title as it was.
void NoteScreen(std::string const &state, bool ready);

// True once after the title was reached from the game with the picture armed,
// then false until the next such return: the entering context names the switch
// on that arrival only.
bool TakeReturnHint();

}  // namespace TitleOverlay
