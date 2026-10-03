#pragma once

#include <string>
#include <vector>

namespace gedcomx {
	struct Note {
		std::string lang;
		std::string subject;
		std::string text;
	};

	struct SourceReference {
		std::string sourceDescriptionId;  // resolved from "#S1" -> "S1"
	};

	// Shared fields every GEDCOM X "conclusion" (Person, Relationship, Fact)
	// carries. Embedded as a member, not a base class - same pattern as Date/
	// PlaceReference.
	struct Conclusion {
		std::string confidence;  // e.g. "http://gedcomx.org/High"
		std::vector<SourceReference> sources;
		std::vector<Note> notes;
		std::string lang;
	};
}