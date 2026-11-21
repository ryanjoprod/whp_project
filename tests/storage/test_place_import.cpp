#include <doctest/doctest.h>

#include <set>

#include "test_helpers.h"

#include "gedcomx/parser.h"
#include "storage/database.h"

namespace {
	// What App does for one file: remap, then save places, then people.
	void importFile(storage::Database& db, const std::string& sourceId, const std::string& placeName) {
		testutil::saveSource(db, sourceId);

		std::vector<gedcomx::PlaceDescription> places(1);
		places[0].id = "PL1";
		places[0].names.push_back({ "", placeName,{} });

		std::vector<gedcomx::Agent> agents;
		std::vector<gedcomx::Person> people(1);
		people[0].id = "PL1";
		gedcomx::Fact f;
		f.type = "http://gedcomx.org/Birth";
		f.place.descriptionId = "PL1";
		people[0].facts.push_back(f);
		std::vector<gedcomx::Relationship> rels;
		std::vector<gedcomx::Event> events;

		gedcomx::remapPlaceAndAgentIds(sourceId, places, agents, people, rels, events);

		db.runInTransaction([&] {
			for (const auto& p : places) db.save(p);
			for (const auto& p : people) db.save(p, sourceId);
		});
	}

	void importDocument(storage::Database& db, const nlohmann::json& doc) {
		auto sources = gedcomx::parseSourceDescriptions(doc);
		auto people = gedcomx::parsePersons(doc);
		auto rels = gedcomx::parseRelationships(doc);
		auto agents = gedcomx::parseAgents(doc);
		auto events = gedcomx::parseEvents(doc);
		auto places = gedcomx::parsePlaceDescriptions(doc);
		auto citations = gedcomx::parseCitations(doc);

		gedcomx::resolveCitationReferences(people, rels, events, places, citations);
		const std::string sourceId = sources[0].id;
		gedcomx::remapPlaceAndAgentIds(sourceId, places, agents, people, rels, events);

		db.runInTransaction([&] {
			for (const auto& s : sources) {
				db.save(s);
				db.ensureBareCitation(s.id);
			}
			for (const auto& c : citations) db.importCitations(c);
			for (const auto& p : places) db.save(p);
			for (const auto& p : people) db.save(p, sourceId);
			for (const auto& r : rels) db.save(r, sourceId);
			for (const auto& a : agents) db.save(a);
			for (const auto& e : events) db.save(e, sourceId);
		});
	}

	nlohmann::json fairfaxFile() {
		return nlohmann::json::parse(R"({
			"sourceDescriptions": [ { "id": "S1", "titles": [ { "value": "Test source" } ] } ],
			"places": [
				{ "id": "PL1", "names": [ { "value": "Virginia" } ],
				  "sources": [ { "description": { "resource": "#S1" } } ] },
				{ "id": "PL2", "names": [ { "value": "Fairfax County" } ],
				  "jurisdiction": { "resource": "#PL1" }, "latitude": 38.8, "longitude": -77.3 },
				{ "id": "PL3", "names": [ { "value": "Unused town" } ] }
			],
			"persons": [ { "id": "P1", "facts": [
				{ "type": "http://gedcomx.org/Birth",
				  "place": { "original": "Fairfax", "description": { "resource": "#PL2" } } } ] } ]
		})");
	}
}

TEST_CASE("Two files that both define PL1 keep both places") {
	storage::Database db(":memory:");
	importFile(db, "S1", "Virginia");
	importFile(db, "S2", "Maryland");

	auto places = db.loadAllPlaces();
	std::set<std::string> ids;
	for (const auto& p : places) ids.insert(p.id);

	std::set<std::string> expected = { "S1:PL1", "S2:PL1" };
	CHECK(ids == expected);
}

TEST_CASE("Importing the same file twice updates its places instead of duplicating them") {
	storage::Database db(":memory:");
	importFile(db, "S1", "Virginia");
	importFile(db, "S1", "Commonwealth of Virginia");

	auto places = db.loadAllPlaces();
	REQUIRE(places.size() == 1);
	CHECK(places[0].primaryName() == "Commonwealth of Virginia");
}

TEST_CASE("Export turns a place id back into the id the file used") {
	storage::Database db(":memory:");
	importFile(db, "S1", "Virginia");

	auto exported = db.gatherSourceExport("S1");
	REQUIRE(exported.persons.size() == 1);
	REQUIRE(exported.persons[0].facts.size() == 1);
	CHECK(exported.persons[0].facts[0].place.descriptionId == "PL1");
}

TEST_CASE("Export does not include another source's places") {
	storage::Database db(":memory:");
	importFile(db, "S1", "Virginia");
	importFile(db, "S2", "Maryland");

	auto exported = db.gatherSourceExport("S1");

	REQUIRE(exported.places.size() == 1);
	CHECK(exported.places[0].id == "PL1");
	CHECK(exported.places[0].primaryName() == "Virginia");
}

TEST_CASE("Export imclude a place the app created, and keeps another source's ancestor id") {
	storage::Database db(":memory:");
	importFile(db, "S1", "Virginia");  // S1:PL1
	importFile(db, "S2", "Maryland");  // S2:PL1

	gedcomx::PlaceDescription made;  // made in the app, no prefix, under S2's place
	made.id = "PL_NEW";
	made.names.push_back({ "", "Annapolis",{} });
	made.jurisdictions.push_back({ "S2:PL1",{} });
	db.save(made);

	gedcomx::Person p;
	p.id = "P9";
	gedcomx::Fact f;
	f.type = "http://gedcomx.org/Death";
	f.place.descriptionId = "PL_NEW";
	p.facts.push_back(f);
	db.save(p, "S1");

	auto exported = db.gatherSourceExport("S1");

	std::set<std::string> ids;
	for (const auto& place : exported.places) ids.insert(place.id);
	std::set<std::string> expected = { "PL1", "PL_NEW", "S2:PL1" };
	CHECK(ids == expected);
}

TEST_CASE("A place file round-trips through import, export and import again") {
	storage::Database first(":memory:");
	importDocument(first, fairfaxFile());
	auto exported = first.gatherSourceExport("S1");
	auto source = first.loadAllSourceDescriptions().at(0);
	auto json = gedcomx::serializeSourceExport(source, exported.persons, exported.relationships, exported.otherSources, exported.citations, exported.events, exported.places);

	storage::Database second(":memory:");
	importDocument(second, json);

	CHECK(second.loadAllPlaces().size() == 3);
	auto again = second.gatherSourceExport("S1");
	REQUIRE(again.persons.size() == 1);
	REQUIRE(again.persons[0].facts.size() == 1);
	CHECK(again.persons[0].facts[0].place.descriptionId == "PL2");
}