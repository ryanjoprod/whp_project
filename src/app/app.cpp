#include <fstream>
#include <queue>
#include <unordered_set>
#include <limits>
#include <algorithm>
#include <iostream>

#include "app.h"
#include "gedcomx/parser.h"

App::App() : db_("test.db"), window_() {
	loadFixtures();

	auto allPeople = db_.loadAll();
	currentRootKey_ = allPeople.empty() ? "" : allPeople[0].id;	// placeholder root choice
	loadNeighborhood(currentRootKey_, neighbothoodDepth_);
}

void App::loadNeighborhood(const std::string& rootPersonKey, int depth) {						

	std::unordered_set<std::string> visitedPersons;
	std::unordered_set<std::string> visitedRelationships;

	std::queue<std::pair<std::string, int>> toVisit;  // (personKey, currentDepth)
	toVisit.push({ rootPersonKey, 0 });
	visitedPersons.insert(rootPersonKey);

	while (!toVisit.empty()) {
		auto [personKey, currentDepth] = toVisit.front();
		toVisit.pop();

		auto person = db_.loadPerson(personKey);
		if (person) visiblePeopleMap_[personKey] = *person;  // union: insert-or-update, nothing removed here

		if (currentDepth >= depth) continue;  //stop expanding past the requested depth

		for (const auto& rel : db_.loadRelationshipsForPerson(personKey)) {
			if (visitedRelationships.insert(rel.id).second) {
				visibleRelationshipsMap_[rel.id] = rel;
			}

			// Whichever side isn't the person we came from is the neighbor to explore next.
			const std::string& neighborKey = (rel.person1Id == personKey) ? rel.person2Id : rel.person1Id;
			if (visitedPersons.insert(neighborKey).second) {
				toVisit.push({ neighborKey, currentDepth + 1 });
			}
		}
	}

	evictFarthestUntilUnderCap();
	rebuildVisibleVectors();
	window_.requestRefit();
}

void App::loadFixtures() {
	std::vector<std::string> files = { "sample_person.json", "sample_person_2.json" };

	for (const auto& filename : files) {
		std::ifstream file(filename);
		nlohmann::json document;
		file >> document;

		auto sourceDescriptions = gedcomx::parseSourceDescriptions(document);
		auto people = gedcomx::parsePersons(document);
		auto relationships = gedcomx::parseRelationships(document);

		const std::string sourceId = sourceDescriptions.empty() ? "" : sourceDescriptions[0].id;

		for (const auto& s : sourceDescriptions) db_.save(s);
		for (const auto& p : people) db_.save(p, sourceId);
		for (const auto& r : relationships) db_.save(r, sourceId);
	}
}

void App::evictFarthestUntilUnderCap() {
	if (visiblePeopleMap_.size() <= kMaxVisibleNodes) return;

	// BFS distance from the *current* root, over the union graph's edges -
	// this reuses the exact same traversal shape as loadNeighborhood itself,
	// just measuring "how far" instead of "which nodes."
	std::unordered_map<std::string, int> distance;
	std::queue<std::string> toVisit;
	toVisit.push(currentRootKey_);
	distance[currentRootKey_] = 0;

	while (!toVisit.empty()) {
		std::string current = toVisit.front();
		toVisit.pop();

		for (const auto& [id, rel] : visibleRelationshipsMap_) {
			if (rel.person1Id != current && rel.person2Id != current) continue;
			const std::string& neighbor = (rel.person1Id == current) ? rel.person2Id : rel.person1Id;
			if (distance.find(neighbor) == distance.end()) {
				distance[neighbor] = distance[current] + 1;
				toVisit.push(neighbor);
			}
		}
	}

	// Anyone not reached by that BFS is disconnected from the current root
	// entirely - treat them as "farthest" so they're evicted first.
	std::vector<std::string> ids;
	for (const auto& [id, person] : visiblePeopleMap_) ids.push_back(id);

	std::sort(ids.begin(), ids.end(),
		[&](const std::string& a, const std::string& b) {
			int da = distance.count(a) ? distance[a] : std::numeric_limits<int>::max();
			int db = distance.count(b) ? distance[b] : std::numeric_limits<int>::max();

			return da > db;  // farthest first
		});

	size_t toRemove = visiblePeopleMap_.size() - kMaxVisibleNodes;
	for (size_t i = 0; i < toRemove; ++i) {
		visiblePeopleMap_.erase(ids[i]);
	}

	// Drop any relationship that now references a removed person.
	for (auto it = visibleRelationshipsMap_.begin(); it != visibleRelationshipsMap_.end(); ) {
		if (!visiblePeopleMap_.count(it->second.person1Id) || !visiblePeopleMap_.count(it->second.person2Id)) {
			it = visibleRelationshipsMap_.erase(it);
		}
		else {
			++it;
		}
	}
}

void App::rebuildVisibleVectors() {
	visiblePeople_.clear();
	for (const auto& [id, person] : visiblePeopleMap_) visiblePeople_.push_back(person);

	visibleRelationships_.clear();
	for (const auto& [id, rel] : visibleRelationshipsMap_) visibleRelationships_.push_back(rel);
}

void App::run() {
	while (window_.isRunning()) {
		window_.processEvents();
		window_.beginFrame();
		window_.renderTitleBarAndMenu();
		window_.renderCanvas(visiblePeople_, visibleRelationships_);
		window_.endFrame();

		std::string requestedRoot = window_.consumeReRootRequest();
		if (!requestedRoot.empty() && requestedRoot != currentRootKey_) {
			currentRootKey_ = requestedRoot;
			loadNeighborhood(currentRootKey_, neighbothoodDepth_);
		}
	}
}