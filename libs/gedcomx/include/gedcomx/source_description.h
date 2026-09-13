#pragma once

#include <string>
#include <vector>

#include "gedcomx/text_value.h"

namespace gedcomx {

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