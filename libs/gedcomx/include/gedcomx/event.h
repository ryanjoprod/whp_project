#pragma once

#include <string>
#include <vector>

#include "gedcomx/fact.h"
#include "gedcomx/conclusion.h"

namespace gedcomx {
	struct EventRole {
		std::string type;  // e.g., "http://gedcomx.org/Principal"
		std::string personId;  // resolved from "#P1" -> "P1"
		std::string details;
		Conclusion conclusion;
	};

	struct Event {
		std::string id;
		std::string type;
		Date date;
		PlaceReference place;
		std::vector<EventRole> roles;
		Conclusion conclusion;
	};
}