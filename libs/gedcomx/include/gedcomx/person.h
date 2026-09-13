#pragma once

#include <string>
#include <vector>

#include "gedcomx/fact.h"
#include "gedcomx/conclusion.h"

namespace gedcomx {
	struct NamePart {
		std::string value;
		std::string type;
	};

	struct NameForm {
		std::string fullText;
		std::vector<NamePart> parts;
	};

	struct Name {
		std::vector<NameForm> nameForms;
	};

	struct Person {
		std::string id;
		std::string sourceId;  // populated by storage on load; empty when freshly parsed
		std::vector<Name> names;
		std::vector<Fact> facts;
		Conclusion conclusion;

		// Convenience accessor - not part of GEDCOM X itself, just useful for
		// display/storage code that wants "the name" witout walking the tree.
		std::string primaryFullText() const {
			if (!names.empty() && !names[0].nameForms.empty()) {
				return names[0].nameForms[0].fullText;
			}

			return "";
		}
	};
}