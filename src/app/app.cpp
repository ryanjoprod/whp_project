#include <fstream>

#include "app.h"
#include "gedcomx/parser.h"

App::App() : db_("test.db"), window_() {
	loadFixtures();
	loadedPeople_ = db_.loadAll();
	loadedRelationships_ = db_.loadAllRelationships();
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

void App::run() {
	while (window_.isRunning()) {
		window_.processEvents();
		window_.beginFrame();
		window_.renderTitleBarAndMenu();
		window_.renderCanvas(loadedPeople_, loadedRelationships_);
		window_.endFrame();
	}
}