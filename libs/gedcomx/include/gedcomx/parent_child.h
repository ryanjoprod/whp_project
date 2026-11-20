#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "gedcomx/relationship.h"

namespace gedcomx {
	inline constexpr const char* kParentChildType = "http://gedcomx.org/ParentChild";
	inline constexpr const char* kBiologicalParent = "http://gedcomx.org/BiologicalParent";
	inline constexpr const char* kAdoptiveParent = "http://gedcomx.org/AdoptiveParent";
	inline constexpr const char* kStepParent = "http://gedcomx.org/StepParent";
	inline constexpr const char* kFosterParent = "http://gedcomx.org/FosterParent";
	inline constexpr const char* kGuardianParent = "http://gedcomx.org/GuardianParent";

	inline constexpr std::array<const char*, 5> kParentageTypes = {
		kBiologicalParent, kAdoptiveParent, kStepParent, kFosterParent, kGuardianParent
	};

	// The parentage fact's type on a ParentChild link ("" if it has none).
	std::string parentageTypeOf(const Relationship& link);
	bool isParentageType(const std::string& type);

	// One staged child, as the editor sees it. All ids are LOCAL (no "S1:" prefix).
	struct ChildLinkInput {
		struct ParentSlot {
			bool isParent = false;  // false = "Not a parent"
			std::string parentageType;  // "" = unspecified (no fact stored)
			std::optional<Relationship> existingLink;  // as loaded from the database, if there is one
		};
		std::string childId;
		ParentSlot parent[2];  // [0] = Person 1, [1] = Person 2
	};

	struct ChildLinkPlan {
		std::vector<Relationship> save;  // new or changed ParentChild links, local ids
		std::vector<std::string> deleteIds;  // local ids of links to delete
	};

	ChildLinkPlan planChildLinks(const std::string& parent1Id,
		const std::string& parent2Id,
		const std::vector<ChildLinkInput>& children,
		const std::vector<Relationship>& removedLinks,
		const std::function<std::string()>& newId);
}