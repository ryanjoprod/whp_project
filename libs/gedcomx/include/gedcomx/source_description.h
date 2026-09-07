#pragma once

#include <string>
#include <vector>

namespace gedcomx {
	//A title or citation string, optionally tagged with a language (BCP 47).
	struct TextValue {
		std::string lang;
		std::string value;
	};

	struct SourceCitation {
		std::string lang;
		std::string value;
	};

	struct SourceDescription {
		std::string id;
		std::vector<TextValue> titles;
		std::vector<SourceCitation> citations;

		// Convenience accessor, same spirit as Person::primaryFullText().
		std::string primaryTitle() const {
			return titles.empty() ? "" : titles[0].value;
		}
	};
}