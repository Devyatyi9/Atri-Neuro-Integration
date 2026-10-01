#include "AudioContext.h"

#include <cstdlib>
#include <vector>

namespace {

// File names reach us in whatever case the scenario wrote them.
std::string Lowered(std::string text) {
	for (char &c : text) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return text;
}

std::string Trimmed(std::string const &text) {
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && (text[begin] == ' ' || text[begin] == '\r')) {
		++begin;
	}
	while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\r')) {
		--end;
	}
	return text.substr(begin, end - begin);
}

std::vector<std::string> Split(std::string const &text, char separator) {
	std::vector<std::string> parts;
	size_t start = 0;
	for (;;) {
		size_t const end = text.find(separator, start);
		parts.push_back(text.substr(start, end - start));
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}
	return parts;
}

bool StartsWith(std::string const &text, char const *prefix, size_t length) {
	return text.compare(0, length, prefix) == 0;
}

void AddLine(std::string &out, std::string const &line) {
	if (!out.empty()) {
		out += "\n";
	}
	out += line;
}

}  // namespace

void AudioContext::LoadTable(std::string const &text) {
	m_Rows.clear();
	for (std::string const &raw : Split(text, '\n')) {
		std::string const line = Trimmed(raw);
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::vector<std::string> const fields = Split(line, '\t');
		std::string const file = Lowered(Trimmed(fields[0]));
		if (file.empty()) {
			continue;
		}
		Row row;
		std::string const report =
		    fields.size() > 1 ? Lowered(Trimmed(fields[1])) : "";
		row.report = report == "start"  ? Report::Start
		             : report == "both" ? Report::Both
		             : report == "off"  ? Report::Off
		                                : Report::Default;
		row.name = fields.size() > 2 ? Trimmed(fields[2]) : "";
		row.description = fields.size() > 3 ? Trimmed(fields[3]) : "";
		m_Rows[file] = row;
	}
}

AudioContext::Row const *AudioContext::Find(std::string const &file) const {
	auto const found = m_Rows.find(Lowered(file));
	return found == m_Rows.end() ? nullptr : &found->second;
}

std::string AudioContext::Words(std::string const &file) const {
	Row const *row = Find(file);
	if (row && !row->description.empty()) {
		return row->description;
	}
	// Not described yet: the name, marked as a name rather than passed off as
	// a description.
	return "[" + (row && !row->name.empty() ? row->name : file) + "]";
}

bool AudioContext::Says(std::string const &file, bool end, bool loops) const {
	Row const *row = Find(file);
	Report const report = row ? row->report : Report::Default;
	switch (report) {
		case Report::Off:
			return false;
		case Report::Start:
			return !end;
		case Report::Both:
			return true;
		case Report::Default:
			break;
	}
	return !end || loops;
}

std::string AudioContext::Describe(std::string const &monitorOutput) {
	std::vector<std::string> shots;
	for (std::string const &line : Split(monitorOutput, '\n')) {
		if (StartsWith(line, "[bgm] ", 6)) {
			m_Music = Trimmed(line.substr(6));
		} else if (line == "[bgm]") {
			m_Music.clear();
		} else if (StartsWith(line, "[bgm_playing] ", 14)) {
			m_MusicPlaying = atoi(line.c_str() + 14) != 0;
		} else if (StartsWith(line, "[loopse]", 8)) {
			m_Loops.clear();
			for (std::string const &file : Split(line.substr(8), ',')) {
				std::string const name = Trimmed(file);
				if (!name.empty()) {
					m_Loops.insert(name);
				}
			}
		} else if (StartsWith(line, "[skip] ", 7)) {
			m_Skipping = atoi(line.c_str() + 7) != 0;
		} else if (StartsWith(line, "[event] se\t", 11)) {
			shots.push_back(Trimmed(line.substr(11)));
		}
	}
	// Skipping runs through sounds faster than they play, and most of them do
	// not play at all. What is still sounding when it ends is said then.
	if (m_Skipping) {
		return std::string();
	}

	std::string out;
	std::string const music = m_MusicPlaying ? m_Music : std::string();
	if (music != m_SaidMusic) {
		if (!m_SayMusic) {
			// Followed all the same, so switching it on later starts clean.
		} else if (!music.empty()) {
			if (Says(music, false, true)) {
				AddLine(out, "You hear music: " + Words(music) + ".");
			}
		} else if (Says(m_SaidMusic, true, true)) {
			AddLine(out, "The music stops.");
		}
		m_SaidMusic = music;
	}
	if (!m_SaySounds) {
		m_SaidLoops = m_Loops;
		return out;
	}
	for (std::string const &file : m_SaidLoops) {
		if (m_Loops.count(file) == 0 && Says(file, true, true)) {
			AddLine(out, "You no longer hear " + Words(file) + ".");
		}
	}
	for (std::string const &file : m_Loops) {
		if (m_SaidLoops.count(file) == 0 && Says(file, false, true)) {
			AddLine(out, "You hear " + Words(file) + ".");
		}
	}
	m_SaidLoops = m_Loops;

	std::set<std::string> once;
	for (std::string const &file : shots) {
		if (!file.empty() && once.insert(file).second && Says(file, false, false)) {
			AddLine(out, "You hear " + Words(file) + ".");
		}
	}
	return out;
}
