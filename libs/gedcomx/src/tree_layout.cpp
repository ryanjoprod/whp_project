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

		// The people grouped the way both steps need them. People are numbered by their place in the input.
		struct Structure {
			std::unordered_map<std::string, size_t> indexOf;
			DisjointSets units;  // partners share a unit, and so a row
			DisjointSets families;  // everyone who is connected to anyone
			std::vector<std::set<size_t>> childUnits;  // indexed by a unit's representative person
			std::vector<std::set<size_t>> parentUnits;
			std::vector<std::set<size_t>> partners;  // indexed by person: the direct couple links

			explicit Structure(size_t count)
				: units(count), families(count), childUnits(count), parentUnits(count), partners(count) {}
		};

		Structure buildStructure(const TreeLayoutInput& input) {
			const size_t count = input.people.size();
			Structure s(count);
			for (size_t i = 0; i < count; ++i)
				s.indexOf.emplace(input.people[i], i);

			for (const auto& couple : input.couples) {
				auto a = s.indexOf.find(couple.first);
				auto b = s.indexOf.find(couple.second);
				if (a == s.indexOf.end() || b == s.indexOf.end() || a->second == b->second) continue;
				s.units.unite(a->second, b->second);
				s.families.unite(a->second, b->second);
				s.partners[a->second].insert(b->second);
				s.partners[b->second].insert(a->second);
			}

			// Parent-child links, as links between units. All couples are joined first, so unit ids are final.
			for (const auto& link : input.parentChild) {
				auto parent = s.indexOf.find(link.first);
				auto child = s.indexOf.find(link.second);
				if (parent == s.indexOf.end() || child == s.indexOf.end()) continue;
				s.families.unite(parent->second, child->second);

				const size_t parentUnit = s.units.find(parent->second);
				const size_t childUnit = s.units.find(child->second);
				if (parentUnit == childUnit) continue;  // bad data: someone is their own partner's child
				s.childUnits[parentUnit].insert(childUnit);
				s.parentUnits[childUnit].insert(parentUnit);
			}

			return s;
		}

		// The row of every person (by index): the root's row is 0
		std::vector<int> rowOfPerson(const TreeLayoutInput& input, Structure& s) {
			const size_t count = input.people.size();

			// Longest chain from the top: a unit is one row below its lowest parent unit.
			std::vector<int> generation(count, 0);
			std::vector<size_t> waiting(count, 0);  // parent units not yet placed
			std::queue<size_t> ready;
			for (size_t i = 0; i < count; ++i) {
				if (s.units.find(i) != i) continue;
				waiting[i] = s.parentUnits[i].size();
				if (waiting[i] == 0) ready.push(i);
			}

			std::vector<size_t> order;  // top to bottom; units stuck in a cycle never gets here and stay on row 0
			while (!ready.empty()) {
				const size_t unit = ready.front();
				ready.pop();
				order.push_back(unit);
				for (size_t child : s.childUnits[unit]) {
					generation[child] = std::max(generation[child], generation[unit] + 1);
					if (--waiting[child] == 0) ready.push(child);
				}
			}

			// A unit with no know parents sites directly above its lowest-placed child, not at the very top
			for (auto it = order.rbegin(); it != order.rend(); ++it) {
				const size_t unit = *it;
				if (!s.parentUnits[unit].empty() || s.childUnits[unit].empty()) continue;
				int lowest = generation[*s.childUnits[unit].begin()];
				for (size_t child : s.childUnits[unit])
					lowest = std::min(lowest, generation[child]);
				generation[unit] = lowest - 1;
			}

			// Each connected group is shifted on its own: the root's group so the root is row 0, any other
			// group so its top row is 0.
			std::unordered_map<size_t, int> shift;
			for (size_t i = 0; i < count; ++i) {
				const size_t family = s.families.find(i);
				const int row = generation[s.units.find(i)];
				auto found = shift.find(family);
				if (found == shift.end())
					shift.emplace(family, row);
				else found->second = std::min(found->second, row);
			}
			auto root = s.indexOf.find(input.rootId);
			if (root != s.indexOf.end())
				shift[s.families.find(root->second)] = generation[s.units.find(root->second)];

			std::vector<int> rows(count);
			for (size_t i = 0; i < count; ++i)
				rows[i] = generation[s.units.find(i)] - shift[s.families.find(i)];

			return rows;
		}

		// A unit's people in a line: partners next to each other, chains in order (B-A-C), nobody twice
		std::vector<size_t> orderMembers(const std::vector<size_t>& members, const std::vector<std::set<size_t>>& partners) {
			std::vector<size_t> ordered;
			std::set<size_t> placed;

			auto walk = [&](size_t start) {
				size_t current = start;
				for (;;) {
					ordered.push_back(current);
					placed.insert(current);
					bool moved = false;
					for (size_t next : partners[current]) {
						if (placed.count(next)) continue;
						current = next;
						moved = true;
						break;
					}
					if (!moved) return;
				}
			};

			for (size_t m : members)  // start from an end of the chain
				if (!placed.count(m) && partners[m].size() <= 1) walk(m);
			for (size_t m : members)  // anything left (a loop of partners)
				if (!placed.count(m)) walk(m);

			return ordered;
		}

		// the non-decreasing sequence closest (least squares) to `values`: pool adjacent violators.
		std::vector<double> nonDecreasingFit(const std::vector<double>& values) {
			struct Block {
				double sum;
				size_t count;
			};

			std::vector<Block> blocks;
			for (double value : values) {
				blocks.push_back({ value, 1 });
				while (blocks.size() > 1) {
					Block& last = blocks[blocks.size() - 1];
					Block& before = blocks[blocks.size() - 2];
					if (before.sum / before.count <= last.sum / last.count) break;
					before.sum += last.sum;
					before.count += last.count;
					blocks.pop_back();
				}
			}

			std::vector<double> fitted;
			for (const auto& block : blocks)
				fitted.insert(fitted.end(), block.count, block.sum / block.count);

			return fitted;
		}

		constexpr double kUnitGap = 0.5;  // empty space between neighboring units, in person-widths
	}

	std::map<std::string, int> assignGenerations(const TreeLayoutInput& input) {
		Structure s = buildStructure(input);
		const std::vector<int> rows = rowOfPerson(input, s);

		std::map<std::string, int> result;
		for (size_t i = 0; i < input.people.size(); ++i)
			result[input.people[i]] = rows[i];

		return result;
	}

	std::map<std::string, TreePoint> layoutTree(const TreeLayoutInput& input) {
		const size_t count = input.people.size();
		std::map<std::string, TreePoint> result;
		if (count == 0)
			return result;

		Structure s = buildStructure(input);
		const std::vector<int> personRow = rowOfPerson(input, s);

		// The units: one per group of partners, in input order.
		std::vector<size_t> unitIds;
		std::vector<std::vector<size_t>> members(count);  // indexed by the unit's representative
		for (size_t i = 0; i < count; ++i) {
			const size_t unit = s.units.find(i);
			if (unit == i)
				unitIds.push_back(i);
			members[unit].push_back(i);
		}
		for (size_t unit : unitIds)
			members[unit] = orderMembers(members[unit], s.partners);

		const int topRow = *std::min_element(personRow.begin(), personRow.end());
		const int bottomRow = *std::max_element(personRow.begin(), personRow.end());
		std::vector<std::vector<size_t>> rows(static_cast<size_t>(bottomRow - topRow + 1));

		// First order: the order the units are found walking out from the root (parents, then children).
		std::vector<bool> seen(count, false);
		std::vector<size_t> discovered;
		auto discoverFrom = [&](size_t start) {
			std::queue<size_t> queue;
			queue.push(start);
			seen[start] = true;
			while (!queue.empty()) {
				const size_t unit = queue.front();
				queue.pop();
				discovered.push_back(unit);
				for (size_t next : s.parentUnits[unit])
					if (!seen[next]) {
						seen[next] = true;
						queue.push(next);
					}
				for (size_t next : s.childUnits[unit])
					if (!seen[next]) {
						seen[next] = true;
						queue.push(next);
					}
			}
		};
		auto rootIt = s.indexOf.find(input.rootId);
		if (rootIt != s.indexOf.end())
			discoverFrom(s.units.find(rootIt->second));
		for (size_t unit : unitIds)
			if (!seen[unit])
				discoverFrom(unit);

		for (size_t unit : discovered)
			rows[static_cast<size_t>(personRow[unit] - topRow)].push_back(unit);

		// Fewer crossings: sort each row by where the units it connects to sit in THEIR rows (as a fraction
		// of the row, so rows of diferent lengths compare), downwards by parents, then upwards by children.
		std::vector<double> fraction(count, 0.0);
		auto refreshFractions = [&]() {
			for (const auto& row : rows)
				for (size_t k = 0; k < row.size(); ++k)
					fraction[row[k]] = (k + 0.5) / row.size();
		};

		auto sortRow = [&](std::vector<size_t>& row, bool byParents) {
			std::vector<std::pair<double, size_t>> keyed;
			for (size_t unit : row) {
				const auto& neighbors = byParents ? s.parentUnits[unit] : s.childUnits[unit];
				double key = fraction[unit];
				if (!neighbors.empty()) {
					double sum = 0.0;
					for (size_t n : neighbors)
						sum += fraction[n];
					key = sum / neighbors.size();
				}
				keyed.emplace_back(key, unit);
			}
			std::stable_sort(keyed.begin(), keyed.end(),
				[](const std::pair<double, size_t>& a, const std::pair<double, size_t>& b) {
					return a.first < b.first;
			});
			for (size_t k = 0; k < row.size(); ++k)
				row[k] = keyed[k].second;
		};
		for (int round = 0; round < 4; ++round) {
			refreshFractions();
			for (size_t r = 1; r < rows.size(); ++r) {
				sortRow(rows[r], true);
				refreshFractions();
			}
			for (int r = static_cast<int>(rows.size()) - 2; r >= 0; --r) {
				sortRow(rows[r], false);
				refreshFractions();
			}
		}

		// Horizontal positions, as the center of each unit.
		std::vector<double> width(count, 0.0);
		std::vector<double> center(count, 0.0);
		for (size_t unit : unitIds)
			width[unit] = static_cast<double>(members[unit].size());
		for (const auto& row : rows) {
			double edge = 0.0;
			for (size_t unit : row) {
				center[unit] = edge + width[unit] / 2.0;
				edge += width[unit] + kUnitGap;
			}
		}

		// ...then pull each unit toward the average of its parents (or children), and fix the row so nothing
		// overlaps and the order is kept.
		auto settle = [&](const std::vector<size_t>& row, bool toParents) {
			std::vector<double> offset(row.size(), 0.0);
			std::vector<double> shifted(row.size(), 0.0);
			for (size_t k = 0; k < row.size(); ++k) {
				if (k > 0)
					offset[k] = offset[k - 1] + (width[row[k - 1]] + width[row[k]]) / 2.0 + kUnitGap;

				const auto& neighbors = toParents ? s.parentUnits[row[k]] : s.childUnits[row[k]];
				double desired = center[row[k]];
				if (!neighbors.empty()) {
					double sum = 0.0;
					for (size_t n : neighbors)
						sum += center[n];
					desired = sum / neighbors.size();
				}
				shifted[k] = desired - offset[k];  // "no overlap" now just means: never decreasing
			}
			const std::vector<double> fitted = nonDecreasingFit(shifted);
			for (size_t k = 0; k < row.size(); ++k)
				center[row[k]] = fitted[k] + offset[k];
		};
		for (int round = 0; round < 6; ++round) {
			for (size_t r = 1; r < rows.size(); ++r)
				settle(rows[r], true);
			for (int r = static_cast<int>(rows.size()) - 2; r >= 0; --r)
				settle(rows[r], false);
		}

		// Spread each unit's people around its center, and put the root at x = 0;
		std::vector<TreePoint> points(count);
		for (size_t unit : unitIds) {
			const double first = center[unit] - (width[unit] - 1.0) / 2.0;
			for (size_t k = 0; k < members[unit].size(); ++k) {
				const size_t person = members[unit][k];
				points[person].x = first + static_cast<double>(k);
				points[person].y = static_cast<double>(personRow[person]);
			}
		}
		const double rootX = rootIt != s.indexOf.end() ? points[rootIt->second].x : 0.0;
		for (size_t i = 0; i < count; ++i) {
			points[i].x -= rootX;
			result[input.people[i]] = points[i];
		}
		return result;
	}
}