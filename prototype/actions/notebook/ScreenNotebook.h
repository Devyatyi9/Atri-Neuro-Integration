#pragma once

#include <string>

#include "ActionSet.h"

// Neuro's notebook, offered on the game screen next to the quick menu's
// actions: opening it swaps open_notebook for the page actions.
namespace ScreenNotebook {

// True when `name` is one of ours and the action has been answered.
bool Handle(std::string const &id,
            std::string const &name,
            std::string const &data);

void AppendActions(ActionSet &set);

// Nonzero while the notebook is open. Part of the capability key, so opening
// and closing re-register the set.
int StateKey();

void Close();

}  // namespace ScreenNotebook
