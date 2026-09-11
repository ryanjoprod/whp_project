#pragma once

#include <vector>
#include <unordered_map>

#include "window.h"
#include "storage/database.h"
#include "gedcomx/person.h"

class App {
public:
	App();
	void run();

private:
	std::string loadSourceFile(const std::string& filePath);

	storage::Database db_;
	Window window_;

	static constexpr size_t kMaxVisibleNodes = 200;  // this is how many people are visible on the screen at once

	std::unordered_map<std::string, gedcomx::Person> visiblePeopleMap_;
	std::unordered_map<std::string, gedcomx::Relationship> visibleRelationshipsMap_;

	std::vector<gedcomx::Person> visiblePeople_;
	std::vector<gedcomx::Relationship> visibleRelationships_;

	std::string currentRootKey_;
	int neighbothoodDepth_ = 2;
	void loadNeighborhood(const std::string& rootPersonKey, int depth);

	void rebuildVisibleVectors();
	void evictFarthestUntilUnderCap();
};