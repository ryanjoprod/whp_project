#pragma once

#include <string>

namespace gedcomx {
	struct Date {
		std::string original;  // human-readable, e.g. "9 April 2001"
		std::string formal;  // GEDCOM X formal date format, e.g. "+2001-04-09"
	};

	struct PlaceReference {
		std::string original;  // human-readable place name, e.g. "Fairfax County, Virginia"
	};

	struct Fact {
		std::string id;
		std::string type;  // e.g. "http://gedcomx.org/Birth"
		Date date;
		PlaceReference place;
		std::string value;  // free-text value, when there's no structured date/place
	};
}