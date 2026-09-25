#pragma once

#include <string>

#include "gedcomx/fact.h"
#include "gedcomx/conclusion.h"

namespace gedcomx {

	struct Relationship {
		std::string id;
		std::string sourceId;
		std::string type;
		std::string person1Id;
		std::string person2Id;
		std::vector<Fact> facts;
		Conclusion conclusion;
	};
}