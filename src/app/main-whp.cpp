#include "storage/database.h"
#include "gedcomx/parser.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <string>

int main() {
	try {
		storage::Database db("test.db");
		std::vector<std::string> files = { "sample_person.json", "sample_person_2.json" };

		for (const auto& filename : files) {
			std::ifstream file(filename);
			nlohmann::json document;
			file >> document;

			auto people = gedcomx::parsePersons(document);
			auto relationships = gedcomx::parseRelationships(document);
			auto sourceDescriptions = gedcomx::parseSourceDescriptions(document);

			const std::string sourceId = sourceDescriptions.empty() ? "" : sourceDescriptions[0].id;

			for (const auto& s : sourceDescriptions) db.save(s);
			for (const auto& p : people) db.save(p, sourceId);
			for (const auto& r : relationships) db.save(r, sourceId);

			std::cout << "Loaded file: " << filename << " (source " << sourceId << ")\n";
		}

		auto loadedPeople = db.loadAll();
		std::cout << "\nAll persons across all sources (" << loadedPeople.size() << "):\n";
		for (const auto& person : loadedPeople) {
			std::cout << "  " << person.id << ": " << person.primaryFullText() << "\n";
		}

		auto loadedSources = db.loadAllSourceDescriptions();
		std::cout << "All sources (" << loadedSources.size() << "):\n";
		for (const auto& src : loadedSources) {
			std::cout << "  " << src.id << ": " << src.primaryTitle() << "\n";
		}

		db.linkPersons("AGG1", {
			{"S1", "P1"},
			{"S2", "P1"}
		});

		auto aggregates = db.loadAggregatePersons();
		std::cout << "\nAggregate persons (" << aggregates.size() << "):\n";
		for (const auto& agg : aggregates) {
			std::cout << "  " << agg.aggregateId << ":\n";
			for (const auto& name : agg.names) {
				for (const auto& form : name.nameForms) {
					std::cout << "    name: " << form.fullText << "\n";
				}
			}
			for (const auto& fact : agg.facts) {
				std::cout << "    fact: " << fact.type << " (" << fact.date.original << ")\n";
			}
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Fatal error: " << e.what() << "\n";
		return 1;
	}

	return 0;
}