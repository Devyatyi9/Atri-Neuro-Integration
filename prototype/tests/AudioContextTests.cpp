#include "AudioContext.h"

#include "TestMain.h"

void RunAudioContextTests() {
	AudioContext audio;
	audio.LoadTable(
	    "# file\treport\tname\tdescription\n"
	    "\n"
	    "BGM01\t\tThe Sunken City\t\n"
	    "bgm02\t\tHope's Light\ta gentle ray of hope through the depths\n"
	    "se171\t\t\train against the windows\n"
	    "se200\tstart\n"
	    "se300\toff\n"
	    "se400\tboth\t\ta door slamming\r\n");
	CHECK(audio.RowCount() == 6);

	// Nothing changed, nothing said.
	CHECK(audio.Describe("").empty());
	CHECK(audio.Describe("[line] 12\n[lang_text] \"x\"\n").empty());

	// A track without a description goes by its name, in brackets; the file
	// name's case does not matter.
	CHECK(audio.Describe("[bgm] BGM01\n[bgm_playing] 1\n") ==
	      "You hear music: [The Sunken City].");
	// Said once, not on every poll.
	CHECK(audio.Describe("[line] 13\n").empty());

	// A described one replaces it without a "stops" in between.
	CHECK(audio.Describe("[bgm] BGM02\n") ==
	      "You hear music: a gentle ray of hope through the depths.");
	CHECK(audio.Describe("[bgm_playing] 0\n") == "The music stops.");
	CHECK(audio.Describe("[bgm_playing] 1\n") ==
	      "You hear music: a gentle ray of hope through the depths.");
	CHECK(audio.Describe("[bgm] \n") == "The music stops.");

	// A track the table does not know goes by its file name.
	CHECK(audio.Describe("[bgm] ed_voonly\n") == "You hear music: [ed_voonly].");

	// Loops come as a set: what joined starts, what left stops.
	CHECK(audio.Describe("[loopse] se171,se200\n") ==
	      "You hear rain against the windows.\nYou hear [se200].");
	// "start" keeps the end quiet; the described one says it.
	CHECK(audio.Describe("[loopse] se999\n") ==
	      "You no longer hear rain against the windows.\nYou hear [se999].");
	CHECK(audio.Describe("[loopse] \n") == "You no longer hear [se999].");

	// One-shot effects: once per poll, "off" never.
	CHECK(audio.Describe("[event] se\tse400\n[event] se\tse400\n"
	                     "[event] se\tse300\n[event] se\tse012\n") ==
	      "You hear a door slamming.\nYou hear [se012].");

	// Nothing is said while skipping; what still sounds is said when it ends.
	CHECK(audio
	          .Describe("[skip] 1\n[bgm] BGM01\n[loopse] se171\n"
	                    "[event] se\tse012\n")
	          .empty());
	CHECK(audio.Describe("[loopse] \n[bgm] BGM02\n").empty());
	CHECK(audio.Describe("[skip] 0\n") ==
	      "You hear music: a gentle ray of hope through the depths.");

	// Music and sounds can be switched off apart.
	AudioContext musicOnly;
	musicOnly.SetKinds(true, false);
	CHECK(musicOnly.Describe("[bgm] BGM09\n[bgm_playing] 1\n[loopse] se171\n"
	                         "[event] se\tse671\n") ==
	      "You hear music: [BGM09].");
	AudioContext soundsOnly;
	soundsOnly.SetKinds(false, true);
	CHECK(soundsOnly.Describe("[bgm] BGM09\n[bgm_playing] 1\n[loopse] se171\n"
	                          "[event] se\tse671\n") ==
	      "You hear [se171].\nYou hear [se671].");
	CHECK(soundsOnly.Describe("[bgm_playing] 0\n").empty());

	// With no table at all every sound still goes by its file name.
	AudioContext bare;
	CHECK(bare.Describe("[bgm] BGM09\n[bgm_playing] 1\n[event] se\tse671\n") ==
	      "You hear music: [BGM09].\nYou hear [se671].");
}
