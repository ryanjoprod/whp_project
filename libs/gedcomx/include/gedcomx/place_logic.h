#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gedcomx/place_description.h"

namespace gedcomx {
	using PlaceIndex = std::unordered_map<std::string, const PlaceDescription*>;

	PlaceIndex indexPlaces(const std::vector<PlaceDescription>& places);

	// "Fairfax, Fairfax County, Virginia": primary names up the FIRST parent chain.
	// The place itself need not be in the index (a new, unsaed place).
	std::string placeTitle(const PlaceDescription& place, const PlaceIndex& index);

	// Every place that has placeId as an ancestor (not placeId itself). These cannot be its parents.
	std::unordered_set<std::string> placeDescendants(const std::string& placeId, const std::vector<PlaceDescription>& places);

	// Empty text means "no value" (out is reset, no error). Returns an error message or "".
	std::string parseCoordinate(const std::string& text, double limit, std::optional<double>& out);
	std::string formatCoordinate(double value);  // up to 6 deciamls, trailing zeros trimmed

	// "" when fine, otherwise the first problem found
	std::string validatePlace(const PlaceDescription& place);
}