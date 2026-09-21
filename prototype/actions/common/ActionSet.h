#pragma once

#include <deque>
#include <string>
#include <vector>

#include "NeuroClient.h"
#include "PluginConfig.h"

// A set of actions under construction, with somewhere to put the strings.
//
// NeuroRegisteredAction holds pointers, not strings, and the registration call
// reads them after the builder returns. Descriptions and schemas that are built
// rather than literal therefore need an owner that outlives the build; when
// every screen built its set inside one function, that owner was a local and
// the rule lived in a comment. Here it is the set itself: `keep` stores the
// string and hands back a pointer that stays valid, because a deque never moves
// what it already holds.
struct ActionSet {
	std::vector<NeuroRegisteredAction> actions;

	char const *keep(std::string text) {
		strings.push_back(std::move(text));
		return strings.back().c_str();
	}

	// An action the config turns off is dropped here rather than at each
	// screen: this is the one door every action goes through, and a screen that
	// forgets the check would offer Neuro something the player switched off.
	void add(char const *name, char const *description, char const *schema) {
		if (PluginConfigActionDisabled(name)) {
			return;
		}
		actions.push_back({name, description, schema});
	}

	NeuroRegisteredAction const *data() const { return actions.data(); }
	int count() const { return static_cast<int>(actions.size()); }
	bool empty() const { return actions.empty(); }

	// The names in order, for the log line each screen writes.
	std::string names() const {
		std::string list;
		for (size_t index = 0; index < actions.size(); ++index) {
			if (index != 0) {
				list += ", ";
			}
			list += actions[index].name;
		}
		return list;
	}

 private:
	std::deque<std::string> strings;
};
