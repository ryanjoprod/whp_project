#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gedcomx {
	// Everything the tree layout needs to know, as plan ids, so it can be tested without the database or the UI
	struct TreeLayoutInput {
		std::vector<std::string> people;  // every person to place
		std::vector<std::pair<std::string, std::string>> couples;  // partners (either one)
		std::vector<std::pair<std::string, std::string>> parentChild;  // (parent, child)
		std::string rootId;  // the person the view is centered on
	};

	// Which row each person goes on: the root's row is 0, ancestors are negative, descendants positive.
	// Partner's share a row; children are always below their parent. Ids the people list does not
	// contain are ignored, and a cycle in the data cannot hang it.
	std::map<std::string, int> assignGenerations(const TreeLayoutInput& input);
}