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

// open_notebook is offered with the quick menu out; the open notebook's own
// actions stay whatever the bar does.
void AppendActions(ActionSet &set, bool quickMenuOut);

// open_notebook as a line of the quick menu reference; empty when the notebook
// is switched off.
std::string ReferenceLine();

// Changes when the notebook opens or closes and when a second one comes into
// hand. Part of the capability key, so each of those re-registers the set.
int StateKey();

// Follows the story from the context monitor's output: which scene and line it
// stands on decides which notebooks are in hand. Game main thread; does not
// block.
void NoteMonitorOutput(std::string const &output);

// Acts on a change in what the story allows: closes a notebook that is no
// longer in hand and tells Neuro when one is gained or lost.
void FollowStory();

// Reaching an ending keeps the notebooks then in hand for later times through.
// The mark lives in the game's system variables, which the caller reads and
// writes: UnlockToSave is the value to write when there is something new, 0
// otherwise; SetUnlocked takes what was read or just written.
int UnlockToSave();
void SetUnlocked(int books);

// A new profile starts with empty notebooks: the folder is renamed with the
// date, on a thread of its own.
void SetAsideNotebooks();

void Close();

}  // namespace ScreenNotebook
