#pragma once

#include <string>
#include <vector>
#include <optional>

#include "gedcomx/fact.h"
#include "gedcomx/conclusion.h"
#include "gedcomx/agent.h"
#include "gedcomx/attribute.h"

namespace gedcomx {
	struct Gender {
		std::string type;
	};

	struct NamePart {
		std::string value;
		std::string type;
	};

	struct NameForm {
		std::string fullText;
		std::vector<NamePart> parts;
	};

	struct Name {
		std::string title;
		std::string nickName;
		std::string callName;
		std::vector<NameForm> nameForms;
	};

	struct Person {
		std::string id;
		std::string sourceId;  // populated by storage on load; empty when freshly parsed
		std::vector<Name> names;
		std::vector<Fact> facts;
		Conclusion conclusion;
		std::optional<Gender> gender;
		std::vector<Attribute> attributes;
		std::vector<Address> addresses;

		// Convenience accessor - not part of GEDCOM X itself, just useful for
		// display/storage code that wants "the name" witout walking the tree.
		std::string primaryFullText() const {
			if (!names.empty() && !names[0].nameForms.empty()) {
				return names[0].nameForms[0].fullText;
			}

			return "";
		}

		// Returns the first fact of the given type, or nullptr if there isn't one.
		// The pointer refers into this Person's own `facts` vector.
		const Fact* findFact(const std::string& type) const {
			for (const auto& f : facts) {
				if (f.type == type) return &f;
			}

			return nullptr;
		}

		const Fact* primaryBirthFact() const { return findFact("http://gedcomx.org/Birth"); }
		const Fact* primaryDeathFact() const { return findFact("http://gedcomx.org/Death"); }
	};
}