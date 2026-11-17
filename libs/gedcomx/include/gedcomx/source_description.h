#pragma once

#include <string>
#include <vector>

#include "gedcomx/text_value.h"
#include "gedcomx/conclusion.h"
#include "gedcomx/attribute.h"

namespace gedcomx {

	struct SourceCitation {
		std::string lang;
		std::string value;
	};

	struct SourceDescription {
		std::string id;
		std::vector<TextValue> titles;
		std::vector<SourceCitation> citations;

		// Not GEDCOM X core: plain text, as in Gramps. (GEDCOM X's own authors and publisher are
		// references to Agents, and the app has no Agent editor.)
		std::string author;
		std::string publication;  // Gramps "Pub. info."
		std::string abbreviation;  // short label for lists and reports
		std::vector<Note> notes;
		std::vector<Attribute> attributes; 

		// Convenience accessor, same spirit as Person::primaryFullText().
		std::string primaryTitle() const {
			return titles.empty() ? "" : titles[0].value;
		}
	};
}