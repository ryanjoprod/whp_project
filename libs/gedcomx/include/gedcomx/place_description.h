#pragma once

#include <string>
#include <vector>
#include <optional>

#include "gedcomx/text_value.h"
#include "gedcomx/conclusion.h"

namespace gedcomx {
	struct PlaceDescription {
		std::string id;
		std::vector<TextValue> names;
		std::string type;
		std::optional<double> latitude;
		std::optional<double> longitude;
		std::string jurisdictionId;  // resolved reference to a parent PlaceDescription's id
		Conclusion conclusion;

		std::string primaryName() const {
			return names.empty() ? "" : names[0].value;
		}
	};
}