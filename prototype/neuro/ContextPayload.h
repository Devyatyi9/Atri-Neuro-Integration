#pragma once

#include <string>

// Turns the semantic events emitted by neuro-assets/context_monitor.tjs into
// the single context message Neuro reads.
//
// The monitor emits one line per event, `[event] <type>\t<fields...>`, with
// free text always last so a fixed number of separators can be split off.
// Everything produced during one poll tick is coalesced into one message: that
// tick is the natural batching window and keeps a fast click-through from
// becoming a burst of separate context messages.
struct ContextPayload {
	std::string message;
	// False marks something worth reacting to (a choice appearing or being
	// taken); dialogue alone is silent, since Neuro is watching rather than
	// being prompted. Mixed ticks are not silent -- the actionable part wins.
	bool silent = true;

	bool Empty() const { return message.empty(); }
};

ContextPayload BuildContextPayload(std::string const &monitorOutput);

// Whether a string shows the reader anything at all.
//
// A speaker name is treated as absent when it is not, which is what this
// answers: the scenario writes some names as an ideographic space, and the game
// draws the name plate only when there is something to draw, so those lines
// look nameless on screen. Testing for an empty string alone puts a colon in
// front of narration.
bool HasVisibleCharacters(std::string const &text);
