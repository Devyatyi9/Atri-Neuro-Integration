#pragma once

#include <map>
#include <set>
#include <string>

// What Neuro is told about sound: the music, the looping background effects and
// the one-shot effects, as lines of text.
//
// The words come from a table, one row per sound file. A row without a
// description is said by its name in brackets, so the table works as a template
// before anything in it is written.
class AudioContext {
 public:
	enum class Report {
		// Whatever suits the kind of sound: music and loops say when they start
		// and when they stop, a one-shot effect only that it played.
		Default,
		Start,
		Both,
		Off,
	};

	struct Row {
		Report report = Report::Default;
		std::string name;
		std::string description;
	};

	// Rows as "file<TAB>report<TAB>name<TAB>description", one per line. Lines
	// starting with '#' and blank lines are skipped; trailing fields may be
	// left out. Replaces whatever was loaded.
	void LoadTable(std::string const &text);

	// Which kinds are spoken of at all: music, and sounds -- the looping ones
	// and the one-shot effects together.
	void SetKinds(bool music, bool sounds) {
		m_SayMusic = music;
		m_SaySounds = sounds;
	}

	// The lines for one poll of the context monitor, joined by line breaks;
	// empty when nothing is to be said. Keeps what is playing between calls.
	std::string Describe(std::string const &monitorOutput);

	size_t RowCount() const { return m_Rows.size(); }

 private:
	Row const *Find(std::string const &file) const;
	std::string Words(std::string const &file) const;
	bool Says(std::string const &file, bool end, bool loops) const;

	std::map<std::string, Row> m_Rows;
	bool m_SayMusic = true;
	bool m_SaySounds = true;
	// As the monitor last reported them.
	std::string m_Music;
	bool m_MusicPlaying = false;
	std::set<std::string> m_Loops;
	bool m_Skipping = false;
	// As Neuro was last told.
	std::string m_SaidMusic;
	std::set<std::string> m_SaidLoops;
};
