#pragma once

#include <string>

// Turns the semantic events emitted by neuro_scripts/context_monitor.tjs into
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
