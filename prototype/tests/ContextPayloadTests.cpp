#include "ContextPayload.h"

#include "TestMain.h"

// The one message Neuro reads for a poll tick. What matters here is not that a
// line survives the trip -- it is what the trip is allowed to change: a record
// that splits in half, a paragraph flattened into a wall, a silent tick that
// should have asked for a decision.

void RunContextPayloadTests() {
	// Nothing happened, so there is nothing to say. An empty message must not
	// become a context message: Neuro would be interrupted five times a second
	// with the news that the game is still there.
	ContextPayload const quiet = BuildContextPayload("");
	CHECK(quiet.Empty());
	CHECK(quiet.silent);

	// Fields are tab-separated with the free text last, so text containing
	// anything except a tab stays in one piece.
	ContextPayload const line =
	    BuildContextPayload("[event] dialogue\tAtri\t\"I'll be your leg!\"\n");
	CHECK(line.message == "Atri: \"I'll be your leg!\"");
	// Dialogue alone is watching, not prompting.
	CHECK(line.silent);

	// A speaker-less line is narration and stands on its own, without a colon
	// hanging off the front of it.
	ContextPayload const narration = BuildContextPayload(
	    "[event] dialogue\t\tCatherine grabs hold of Atri's hand.\n");
	CHECK(narration.message == "Catherine grabs hold of Atri's hand.");

	// The diary inserts are written in paragraphs, and the player sees them
	// that way. A break cannot travel raw -- one event is one line, so it would
	// end the record halfway and leave the rest looking like an event of its
	// own -- so it arrives escaped and is restored here.
	ContextPayload const diary = BuildContextPayload(
	    "[event] dialogue\t\txx/xx/xx \\nI woke up.\\n\\nAn order.\n");
	CHECK(diary.message == "xx/xx/xx \nI woke up.\n\nAn order.");

	// A lone backslash is not an escape and must survive as itself.
	ContextPayload const backslash =
	    BuildContextPayload("[event] dialogue\t\tone \\ two\n");
	CHECK(backslash.message == "one \\ two");

	// A choice is the one thing that is actually asking for something.
	ContextPayload const choice = BuildContextPayload(
	    "[event] choice_shown\tLet Atri go.\tTake care of Atri.\n");
	CHECK(choice.message ==
	      "A choice appeared: 1) Let Atri go. 2) Take care of Atri.");
	CHECK(!choice.silent);

	// Everything in one tick becomes one message, in order. The actionable part
	// decides the whole tick: a choice that appeared alongside dialogue still
	// has to be answered.
	ContextPayload const mixed = BuildContextPayload(
	    "[event] dialogue\tAtri\t\"Umm, err...\"\n"
	    "[event] choice_shown\tLet Atri go.\tTake care of Atri.\n");
	CHECK(mixed.message ==
	      "Atri: \"Umm, err...\"\n"
	      "A choice appeared: 1) Let Atri go. 2) Take care of Atri.");
	CHECK(!mixed.silent);

	// The monitor emits far more than events -- every changed field goes down
	// the same pipe. Those are for the log, not for Neuro.
	ContextPayload const fields =
	    BuildContextPayload("[scenario] b102.ks\n[line] 1582\n[bgm] BGM09\n");
	CHECK(fields.Empty());

	// An event name this build does not know is dropped rather than forwarded
	// raw: the context is a curated surface, not a debug stream.
	ContextPayload const unknown =
	    BuildContextPayload("[event] weather_changed\train\n");
	CHECK(unknown.Empty());

	// An event with its text missing says nothing, and saying nothing is not
	// the same as saying "".
	ContextPayload const empty =
	    BuildContextPayload("[event] dialogue\tAtri\t\n");
	CHECK(empty.Empty());

	// A speaker the reader cannot see is no speaker. The prologue writes some
	// names as an ideographic space, and the game draws no name plate for them,
	// so a colon in front of the line would be ours rather than the game's.
	ContextPayload const ideographic = BuildContextPayload(
	    "[event] dialogue\t\xE3\x80\x80\tThis world is sinking.\n");
	CHECK(ideographic.message == "This world is sinking.");

	// Several of them, mixed with ordinary spaces, still show nothing.
	ContextPayload const padded = BuildContextPayload(
	    "[event] dialogue\t \xE3\x80\x80 \xE3\x80\x80\tStill nameless.\n");
	CHECK(padded.message == "Still nameless.");

	// A real name that merely starts with one keeps its colon: the question is
	// whether anything shows, not whether it starts with a space.
	// The literals are split because a hex escape in C++ swallows every hex
	// digit that follows it, and "A" is one.
	ContextPayload const paddedName = BuildContextPayload(
	    "[event] dialogue\t\xE3\x80\x80"
	    "Atri\tI am here.\n");
	CHECK(paddedName.message ==
	      "\xE3\x80\x80"
	      "Atri: I am here.");
}

void RunVisibleCharacterTests() {
	CHECK(!HasVisibleCharacters(""));
	CHECK(!HasVisibleCharacters(" "));
	CHECK(!HasVisibleCharacters("\t\r\n"));
	CHECK(!HasVisibleCharacters("\xE3\x80\x80"));
	CHECK(!HasVisibleCharacters("\xE3\x80\x80\xE3\x80\x80"));
	CHECK(HasVisibleCharacters("A"));
	CHECK(HasVisibleCharacters(" A "));
	CHECK(
	    HasVisibleCharacters("\xE3\x80\x80"
	                         "A"));
	// A truncated sequence is not the ideographic space and must not be eaten
	// as one. Whatever it is, it is not nothing.
	CHECK(HasVisibleCharacters("\xE3\x80"));
}
