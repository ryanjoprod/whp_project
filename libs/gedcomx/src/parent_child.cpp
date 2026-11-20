#include "gedcomx/parent_child.h"

#include <algorithm>

namespace gedcomx {
	std::string parentageTypeOf(const Relationship& link) {
		for (const auto& f : link.facts)
			if (isParentageType(f.type)) return f.type;

		return "";
	}

	bool isParentageType(const std::string& type) {
		return std::any_of(kParentageTypes.begin(), kParentageTypes.end(),
			[&](const char* t) {
				return type == t;
			});
	}

	ChildLinkPlan planChildLinks(const std::string& parent1Id,
		const std::string& parent2Id,
		const std::vector<ChildLinkInput>& children,
		const std::vector<Relationship>& removedLinks,
		const std::function<std::string()>& newId
	) {
		ChildLinkPlan plan;
		const std::string parentIds[2] = { parent1Id, parent2Id };

		for (const auto& child : children) {
			if (child.childId == parent1Id || child.childId == parent2Id) continue;  // nobody is their own parent

			for (int slot = 0; slot < 2; ++slot) {
				if (parentIds[slot].empty()) continue;  // single-parent mode: there is no second parent
				const auto& p = child.parent[slot];

				if (!p.isParent) {
					if (p.existingLink)
						plan.deleteIds.push_back(p.existingLink->id);
					continue;
				}

				Relationship link;
				if (p.existingLink)
					link = *p.existingLink;  // keeps id, citations, other facts
				else
					link.id = newId();

				link.type = kParentChildType;
				link.person1Id = parentIds[slot];  // parent
				link.person2Id = child.childId;  // child

				std::erase_if(link.facts, [](const Fact& f) { return isParentageType(f.type); });
				if (!p.parentageType.empty()) {
					Fact f;
					f.type = p.parentageType;
					link.facts.push_back(std::move(f));
				}

				plan.save.push_back(std::move(link));
			}
		}

		for (const auto& l : removedLinks)
			plan.deleteIds.push_back(l.id);

		return plan;
	}
}