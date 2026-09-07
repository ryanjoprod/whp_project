#pragma once

#include <vector>

#include "window.h"
#include "storage/database.h"
#include "gedcomx/person.h"

class App {
public:
	App();
	void run();

private:
	void loadFixtures();

	storage::Database db_;
	Window window_;

	std::vector<gedcomx::Person> loadedPeople_;
	std::vector<gedcomx::Relationship> loadedRelationships_;
};