#include <algorithm>
#include <queue>
#include <set>
#include <unordered_map>

#include "gedcomx/tree_layout.h"

namespace gedcomx {
	namespace {
		// Union-find over small integers
		class DisjointSets {
		public:
			explicit DisjointSets(size_t count) : parent_(count) {
				for (size_t i = 0; i < count; ++i)
					parent_[i] = i;
			}

			size_t find(size_t x) {
				while (parent_[x] != x) {
					parent_[x] = parent_[parent_[x]];
					x = parent_[x];
				}
				return x;
			}

			void unite(size_t a, size_t b) {
				parent_[find(a)] = find(b);
			}

		private:
			std::vector<size_t> parent_;
		};
	}

	std::map<std::string, int> assignGenerations(const TreeLayoutInput& input) {
		const size_t count = input.people.size();

		std::unordered_map<std::string, size_t> indexOf;
		for (size_t i = 0; i < count; ++i)
			indexOf.emplace(input.people[i], i);

		DisjointSets units(count);  // partners share a unit, and so a row
		DisjointSets families(count);  // everyone who is connected to anyone

		for (const auto& couple : input.couples) {
			auto a = indexOf.find(couple.first);
			auto b = indexOf.find(couple.second);
			if (a == indexOf.end() || b == indexOf.end()) continue;
			units.unite(a->second, b->second);
			families.unite(a->second, b->second);
		}

		// Parent-child links, as links between units. All couple are joined first, so the unit ids are final.
		std::vector<std::set<size_t>> childUnits(count);
		std::vector<std::set<size_t>> parentUnits(count);
		for (const auto& link : input.parentChild) {
			auto parent = indexOf.find(link.first);
			auto child = indexOf.find(link.second);
			if (parent == indexOf.end() || child == indexOf.end()) continue;
			families.unite(parent->second, child->second);

			const size_t parentUnit = units.find(parent->second);
			const size_t childUnit = units.find(child->second);
			if (parentUnit == childUnit) continue;  // bad data: someone is their own partner's child
			childUnits[parentUnit].insert(childUnit);
			parentUnits[childUnit].insert(parentUnit);
		}

		// Longest chain from the top: a unit is one row below its lowest parent unit.
		std::vector<int> generation(count, 0);
		std::vector<size_t> waiting(count, 0);  // parent units not yet placed
		std::queue<size_t> ready;
		for (size_t i = 0; i < count; ++i) {
			if (units.find(i) != i) continue;
			waiting[i] = parentUnits[i].size();
			if (waiting[i] == 0)
				ready.push(i);
		}

		std::vector<size_t> order;  // top to bottom; units stuck in a cycle never get here and stay on row 0
		while (!ready.empty()) {
			const size_t unit = ready.front();
			ready.pop();
			order.push_back(unit);
			for (size_t child : childUnits[unit]) {
				generation[child] = std::max(generation[child], generation[unit] + 1);
				if (--waiting[child] == 0)
					ready.push(child);
			}
		}

		// A unit with no known parents sits directly above its lowest-placed child, not at the very top.
		for (auto it = order.rbegin(); it != order.rend(); ++it) {
			const size_t unit = *it;
			if (!parentUnits[unit].empty() || childUnits[unit].empty()) continue;
			int lowest = generation[*childUnits[unit].begin()];
			for (size_t child : childUnits[unit])
				lowest = std::min(lowest, generation[child]);
			generation[unit] = lowest - 1;
		}

		// Each connected group is shifted on its own: the root's group so the root is row 0, any other
		// group so its top row is 0.
		std::unordered_map<size_t, int> shift;
		for (size_t i = 0; i < count; ++i) {
			const size_t family = families.find(i);
			const int row = generation[units.find(i)];
			auto found = shift.find(family);
			if (found == shift.end()) shift.emplace(family, row);
			else found->second = std::min(found->second, row);
		}
		auto root = indexOf.find(input.rootId);
		if (root != indexOf.end())
			shift[families.find(root->second)] = generation[units.find(root->second)];

		std::map<std::string, int> result;
		for (size_t i = 0; i < count; ++i)
			result[input.people[i]] = generation[units.find(i)] - shift[families.find(i)];

		return result;
	}
}