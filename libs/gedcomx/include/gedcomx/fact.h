#pragma once

#include <string>

#include "gedcomx/conclusion.h"

namespace gedcomx {
	struct Date {
		std::string original;  // human-readable, e.g. "9 April 2001"
		std::string formal;  // GEDCOM X formal date format, e.g. "+2001-04-09"
	};

	struct PlaceReference {
		std::string original;  // human-readable place name, e.g. "Fairfax County, Virginia"
		std::string descriptionId;  // optional link to a PlaceDescription, resolved from "#PL1" -> "PL1"
	};

	struct Fact {
		std::string id;
		std::string type;  // e.g. "http://gedcomx.org/Birth"
		Date date;
		PlaceReference place;
		std::string value;  // free-text value, when there's no structured date/place
		Conclusion conclusion;
	};

	// Converts a GEDCOM X type URI (e.g. "http://gedcomx.org/Birth") into a
	// humaan-readable label ("Birth"). Falls back to the raw string unchanged
	// it if doesn't match the expected GEDCOM X URI shape.
	std::string formatTypeUri(const std::string& typeUri);
}