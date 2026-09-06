#include "ContextPayload.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

constexpr char kEventPrefix[] = "[event] ";
constexpr size_t kEventPrefixLength = sizeof(kEventPrefix) - 1;

std::vector<std::string> SplitFields(std::string const &line) {
	std::vector<std::string> fields;
	size_t start = 0;
	while (true) {
		size_t const separator = line.find('\t', start);
		if (separator == std::string::npos) {
			fields.push_back(line.substr(start));
			break;
		}
		fields.push_back(line.substr(start, separator - start));
		start = separator + 1;
	}
	return fields;
}

// Line breaks arrive escaped and are put back here.
//
// A dialogue event is one line with tab-separated fields, so a break carried
// raw would end the record halfway through a sentence and leave the rest
// looking like an event of its own. The diary inserts are written in
// paragraphs, and that shape is part of what the player reads, so it is worth
// carrying rather than flattening.
std::string RestoreBreaks(std::string const &text) {
	std::string restored;
	restored.reserve(text.size());
	for (size_t index = 0; index < text.size(); ++index) {
		if (text[index] == '\\' && index + 1 < text.size() &&
		    text[index + 1] == 'n') {
			restored += '\n';
			++index;
			continue;
		}
		restored += text[index];
	}
	return restored;
}

// Dialogue text carries its own quotation marks in the scenario data, so none
// are added here; a speaker-less line is narration and stands on its own.
std::string DescribeDialogue(std::vector<std::string> const &fields) {
	if (fields.size() < 3) {
		return {};
	}
	std::string const &speaker = fields[1];
	std::string const text = RestoreBreaks(fields[2]);
	if (text.empty()) {
		return {};
	}
	return speaker.empty() ? text : speaker + ": " + text;
}

std::string DescribeChoiceShown(std::vector<std::string> const &fields) {
	if (fields.size() < 2) {
		return {};
	}
	std::string description = "A choice appeared:";
	for (size_t index = 1; index < fields.size(); ++index) {
		if (fields[index].empty()) {
			continue;
		}
		description += " " + std::to_string(index) + ") " + fields[index];
	}
	return description;
}

std::string DescribeChoiceMade(std::vector<std::string> const &fields) {
	if (fields.size() < 2 || fields[1].empty()) {
		return {};
	}
	return "Chose: " + fields[1];
}

}  // namespace

ContextPayload BuildContextPayload(std::string const &monitorOutput) {
	ContextPayload payload;

	size_t lineStart = 0;
	while (lineStart < monitorOutput.size()) {
		size_t lineEnd = monitorOutput.find('\n', lineStart);
		if (lineEnd == std::string::npos) {
			lineEnd = monitorOutput.size();
		}
		std::string const line =
		    monitorOutput.substr(lineStart, lineEnd - lineStart);
		lineStart = lineEnd + 1;

		// Every other line is a polled state field, which stays research-only
		// output; only whitelisted semantic events reach Neuro.
		if (line.compare(0, kEventPrefixLength, kEventPrefix) != 0) {
			continue;
		}

		std::vector<std::string> const fields =
		    SplitFields(line.substr(kEventPrefixLength));
		if (fields.empty()) {
			continue;
		}

		std::string description;
		bool actionable = false;
		if (fields[0] == "dialogue") {
			description = DescribeDialogue(fields);
		} else if (fields[0] == "choice_shown") {
			description = DescribeChoiceShown(fields);
			actionable = true;
		} else if (fields[0] == "choice_made") {
			description = DescribeChoiceMade(fields);
			actionable = true;
		} else {
			// Unknown event names are dropped rather than forwarded raw: the
			// context is a curated product surface, not a debug stream.
			continue;
		}

		if (description.empty()) {
			continue;
		}
		if (!payload.message.empty()) {
			payload.message += "\n";
		}
		payload.message += description;
		if (actionable) {
			payload.silent = false;
		}
	}

	return payload;
}
