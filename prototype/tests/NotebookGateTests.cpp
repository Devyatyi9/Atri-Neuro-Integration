#include "NotebookGate.h"

#include "TestMain.h"

using NotebookGate::At;
using NotebookGate::Reason;

void RunNotebookGateTests() {
	// Before she first writes in it there is no notebook, the morning she buys
	// it included.
	CHECK(At("b999.ks", 10).reason == Reason::NotYet);
	CHECK(At("b113.ks", 5000).reason == Reason::NotYet);
	CHECK(At("b114.ks", 1643).reason == Reason::NotYet);
	CHECK(!At("b114.ks", 1643).Any());

	// The first, from that line until it is lost.
	CHECK(At("b114.ks", 1644).first && !At("b114.ks", 1644).second);
	CHECK(At("b207.ks", 1).first);
	CHECK(At("b304.ks", 2209).first);

	// Lost at the end of one scene, into the start of the next.
	CHECK(At("b304.ks", 2210).reason == Reason::Lost);
	CHECK(At("b401.ks", 1).reason == Reason::Lost);
	CHECK(!At("b401.ks", 983).Any());

	// The second alone, from the shop until the first is found.
	CHECK(At("b401.ks", 984).second && !At("b401.ks", 984).first);
	CHECK(At("b405.ks", 1).second && !At("b405.ks", 1).first);
	CHECK(!At("b407.ks", 2122).first);

	// Both from the island on, through the happy and the true ending.
	CHECK(At("b407.ks", 2123).Both());
	CHECK(At("b501.ks", 1).Both());
	CHECK(At("b701.ks", 1).Both());
	CHECK(At("b501.ks", 1).reason == Reason::InHand);

	// The bad ending never reaches the island.
	CHECK(At("b601.ks", 1).second && !At("b601.ks", 1).first);

	// Scene order is not number order: the prologue is b999.
	CHECK(At("b999.ks", 9999).reason == Reason::NotYet);

	// Anything that is not a scene of the story.
	CHECK(At("title.ks", 1).reason == Reason::NotInStory);
	CHECK(At("", 0).reason == Reason::NotInStory);
	CHECK(At("b304", 1).first);

	// The happy and the true ending keep the notebooks; the bad one does not.
	CHECK(NotebookGate::KeepsNotebooks("b501.ks") &&
	      NotebookGate::KeepsNotebooks("b701"));
	CHECK(!NotebookGate::KeepsNotebooks("b601.ks") &&
	      !NotebookGate::KeepsNotebooks("b407.ks") &&
	      !NotebookGate::KeepsNotebooks(""));

	CHECK(NotebookGate::SceneIndex("b999.ks") == 0);
	CHECK(NotebookGate::SceneIndex("b101.ks") == 1);
	CHECK(NotebookGate::SceneIndex("title.ks") == -1);

	// Playing on leads to the next scene in order, and from b403 to the bad
	// ending as well; anything else is a jump.
	auto index = [](char const *scene) {
		return NotebookGate::SceneIndex(scene);
	};
	CHECK(NotebookGate::FollowsOn(index("b304"), index("b401")));
	CHECK(NotebookGate::FollowsOn(index("b999"), index("b101")));
	CHECK(NotebookGate::FollowsOn(index("b407"), index("b501")));
	CHECK(NotebookGate::FollowsOn(index("b403"), index("b601")));
	CHECK(NotebookGate::FollowsOn(index("b403"), index("b404")));
	CHECK(!NotebookGate::FollowsOn(index("b201"), index("b404")));
	CHECK(!NotebookGate::FollowsOn(index("b404"), index("b403")));
	CHECK(!NotebookGate::FollowsOn(index("b501"), index("b601")));
	CHECK(!NotebookGate::FollowsOn(-1, index("b101")));
}
