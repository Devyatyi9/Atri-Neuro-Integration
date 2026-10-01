#pragma once

#include <string>

// Which notebooks are in hand at a point of the story.
//
// Neuro has a notebook when Atri does: the first from the night she starts
// writing in it until she loses it, the second from the shop where she buys
// another, and the first again once it is found.
namespace NotebookGate {

enum class Reason {
	// At least one notebook is in hand.
	InHand,
	// The story has not reached the first notebook.
	NotYet,
	// The first is lost and the second not bought yet.
	Lost,
	// Not a scene of the story: a menu, a replay from the gallery.
	NotInStory,
};

struct Access {
	bool first = false;
	bool second = false;
	Reason reason = Reason::NotInStory;

	bool Any() const { return first || second; }
	bool Both() const { return first && second; }
};

// `scenario` is the scene's file name, "b304.ks"; `line` is the scenario line
// the conductor stands on.
Access At(std::string const &scenario, int line);

// The scene's place in the order they are played, or -1 when it is not one of
// the story's.
int SceneIndex(std::string const &scenario);

// Whether the scene is one of the endings: reaching one is what keeps the
// notebooks in hand for the next time through.
bool IsEnding(std::string const &scenario);

}  // namespace NotebookGate
