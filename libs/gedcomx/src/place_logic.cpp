#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gedcomx/place_logic.h"

namespace {
	std::string trimmed(const std::string& s) {
		const auto first = s.find_first_not_of("\t\r\n");
		if (first == std::string::npos) return "";
		const auto last = s.find_last_not_of(" \t\r\n");
		return s.substr(first, last - first + 1);
	}
}

namespace gedcomx {
	PlaceIndex indexPlaces(const std::vector<PlaceDescription>& places) {
		PlaceIndex index;
		for (const auto& p : places)
			index.emplace(p.id, &p);
		return index;
	}

	std::string placeTitle(const PlaceDescription& place, const PlaceIndex& index) {
		std::string title;
		std::unordered_set<std::string> seen;
		const PlaceDescription* current = &place;

		while (current) {
			if (!seen.insert(current->id).second) break;  // never loops, even on bad data

			const std::string name = current->primaryName();
			if (!name.empty()) {
				if (!title.empty())
					title += ", ";
				title += name;
			}

			if (current->jurisdictions.empty()) break;
			auto it = index.find(current->jurisdictions.front().parentId);
			current = it == index.end() ? nullptr : it->second;
		}

		return title.empty() ? "(unnamed place)" : title;
	}

	std::unordered_set<std::string> placeDescendants(const std::string& placeId, const std::vector<PlaceDescription>& places) {
		std::unordered_map<std::string, std::vector<std::string>> children;
		for (const auto& p : places)
			for (const auto& j : p.jurisdictions)
				children[j.parentId].push_back(p.id);

		std::unordered_set<std::string> found;
		std::vector<std::string> todo{ placeId };
		while (!todo.empty()) {
			const std::string id = todo.back();
			todo.pop_back();
			auto it = children.find(id);
			if (it == children.end()) continue;
			for (const auto& child : it->second)
				if (child != placeId && found.insert(child).second)
					todo.push_back(child);
		}
		return found;
	}

	std::string parseCoordinate(const std::string& text, double limit, std::optional<double>& out) {
		out.reset();
		const std::string t = trimmed(text);
		if (t.empty()) return "";

		char* end = nullptr;
		const double value = std::strtod(t.c_str(), &end);
		if (end == t.c_str() || *end != '\0' || !std::isfinite(value))
			return "Enter a number in decimal degrees, like 38.8462.";
		if (std::fabs(value) > limit) {
			char message[64];
			std::snprintf(message, sizeof(message), "Must be ebtween -%g and %g.", limit, limit);
			return message;
		}
		out = value;
		return "";
	}

	std::string formatCoordinate(double value) {
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.6f", value);
		std::string s = buffer;
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
		if (s == "-0") s = "0";
		return s;
	}

	std::string validatePlace(const PlaceDescription& p) {
		bool anyName = false;
		for (const auto& n : p.names)
			if (!trimmed(n.value).empty()) anyName = true;
		if (!anyName) return "A place needs at least one name.";

		for (const auto& u : p.urls)
			if (trimmed(u.url).empty()) return "A web link is empty.";

		for (const auto& j : p.jurisdictions) {
			if (j.parentId.empty()) return "A parent place is not chosen.";
			if (!p.id.empty() && j.parentId == p.id) return "A place cannot be its own parent.";
		}

		return "";
	}
}