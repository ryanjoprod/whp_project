#include <doctest/doctest.h>

#include <set>

#include "storage/database.h"
#include "gedcomx/parser.h"

TEST_CASE("Source export gathers and serializes correctly") {
	storage::Database db(":memory:");

	gedcomx::SourceDescription source;
	source.id = "S1";
	source.titles.push_back({ "", "Test Sources" });
	db.save(source);

	gedcomx::Person p1;
	p1.id = "P1";
	p1.names.push_back({});
	p1.names[0].nameForms.push_back({ "Alice", {} });
	db.save(p1, "S1");

	gedcomx::Person p2;
	p2.id = "P2";
	p2.names.push_back({});
	p2.names[0].nameForms.push_back({ "Bob", {} });
	db.save(p2, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = "P1";
	rel.person2Id = "P2";
	db.save(rel, "S1");

	auto exportData = db.gatherSourceExport("S1");
	REQUIRE(exportData.persons.size() == 2);
	REQUIRE(exportData.relationships.size() == 1);

	// Confirm ids were correctly stripped back to bare local ids, not row-keys.
	std::set<std::string> ids{ exportData.persons[0].id, exportData.persons[1].id };
	CHECK(ids == std::set<std::string>{ "P1", "P2" });
	CHECK(exportData.relationships[0].person1Id == "P1");
	CHECK(exportData.relationships[0].person2Id == "P2");

	nlohmann::json doc = gedcomx::serializeSourceExport(source, exportData.persons, exportData.relationships);
	auto reparsedPersons = gedcomx::parsePersons(doc);
	auto reparsedRels = gedcomx::parseRelationships(doc);

	REQUIRE(reparsedPersons.size() == 2);
	REQUIRE(reparsedRels.size() == 1);
	CHECK(reparsedRels[0].person1Id == "P1");
	CHECK(reparsedRels[0].person2Id == "P2");
}