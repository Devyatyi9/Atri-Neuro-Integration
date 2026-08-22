#pragma once

#include <string>

#include "ActionSet.h"

// The screens that are neither the title nor the story: the settings screen and
// its tabs, any screen that only carries a way back, the modal confirmation
// dialog, and the language screen the game shows once on the very first launch.
namespace ScreenMenu {

// True when `name` is one of ours and the action has been answered.
bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data);

// A settings-style screen. `tabs` is how many tab buttons it carries, zero for
// the screens that have none; `canReturnToTitle` follows the "title" control.
void AppendMenuActions(ActionSet &set, int tabs, bool canReturnToTitle);

// The modal dialog owns the screen while it is up: answering it is the only
// thing that can succeed.
void AppendDialogActions(ActionSet &set);

// The first-run language screen, and the notice that explains it. The notice is
// retried because that screen is up while the SDK may still be connecting.
void AppendLanguageActions(ActionSet &set);
bool TrySendLanguageNotice();
void ResetLanguageNotice();

}  // namespace ScreenMenu
