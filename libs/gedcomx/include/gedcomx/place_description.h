#pragma once

#include <string>
#include <vector>
#include <optional>

#include "gedcomx/conclusion.h"
#include "gedcomx/fact.h"  // for Date

namespace gedcomx {
	struct PlaceName {
		std::string lang;
		std::string value;
		Date date;  // when this name was in use
	};

	struct PlaceJurisdiction {
		std::string parentId;
		Date date;  // when the place belonged to this parent
	};

	struct PlaceUrl {
		std::string url;
		std::string description;
		std::string type;
	};

	struct PlaceDescription {
		std::string id;
		std::vector<PlaceName> names;
		std::string type;
		std::optional<double> latitude;
		std::optional<double> longitude;
		std::string code;
		std::vector<PlaceJurisdiction> jurisdictions;
		std::vector<PlaceUrl> urls;
		Conclusion conclusion;

		std::string primaryName() const {
			return names.empty() ? "" : names[0].value;
		}
	};
}