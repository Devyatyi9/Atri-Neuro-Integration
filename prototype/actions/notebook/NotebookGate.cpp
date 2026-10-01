#include "NotebookGate.h"

namespace {

// The scenes in the order they are played, which their numbers do not give:
// the prologue is b999, and the endings branch.
char const *const kOrder[] = {
    "b999", "b101", "b102", "b103", "b111", "b112", "b113", "b114", "b121",
    "b122", "b123", "b124", "b200", "b201", "b202", "b203", "b204", "b205",
    "b206", "b207", "b301", "b302", "b303", "b304", "b401", "b402", "b403",
    "b404", "b405", "b406", "b407", "b501", "b601", "b701",
};

// Where in a scene each turn happens, as its scenario line.
struct Mark {
	char const *scene;
	int line;
};
// She takes the notebook out and writes for the first time.
constexpr Mark kFirstWritten = {"b114", 1644};
// "I don't have my logbook."
constexpr Mark kFirstLost = {"b304", 2210};
// She shows the one she bought.
constexpr Mark kSecondBought = {"b401", 984};
// The first is found on the island and handed over.
constexpr Mark kFirstFound = {"b407", 2123};

int IndexOf(std::string const &scene) {
	for (int index = 0; index < (int)(sizeof(kOrder) / sizeof(kOrder[0]));
	     ++index) {
		if (scene == kOrder[index]) {
			return index;
		}
	}
	return -1;
}

// Whether the position is at the mark or past it.
bool Reached(int scene, int line, Mark const &mark) {
	int const at = IndexOf(mark.scene);
	return scene > at || (scene == at && line >= mark.line);
}

}  // namespace

namespace NotebookGate {

Access At(std::string const &scenario, int line) {
	Access access;
	size_t const dot = scenario.find('.');
	std::string const scene =
	    dot == std::string::npos ? scenario : scenario.substr(0, dot);
	int const index = IndexOf(scene);
	if (index < 0) {
		return access;
	}
	if (scene == "b601") {
		// The bad ending leaves before the first is found.
		access.second = true;
	} else if (!Reached(index, line, kFirstWritten)) {
		access.reason = Reason::NotYet;
		return access;
	} else if (!Reached(index, line, kFirstLost)) {
		access.first = true;
	} else if (!Reached(index, line, kSecondBought)) {
		access.reason = Reason::Lost;
		return access;
	} else {
		access.second = true;
		access.first = Reached(index, line, kFirstFound);
	}
	access.reason = Reason::InHand;
	return access;
}

bool IsEnding(std::string const &scenario) {
	std::string const scene = scenario.substr(0, scenario.find('.'));
	return scene == "b501" || scene == "b601" || scene == "b701";
}

}  // namespace NotebookGate
