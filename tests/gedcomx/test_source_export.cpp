#include <doctest/doctest.h>

#include <set>

#include "storage/database.h"
#include "gedcomx/parser.h"
#include "gedcomx/citation.h"

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

TEST_CASE("Citations export as component descriptions and import back") {
	gedcomx::SourceDescription source;
	source.id = "S1";
	source.titles.push_back({ "", "Census" });
	gedcomx::SourceDescription other;
	other.id = "S2";
	other.titles.push_back({ "", "Parish" });

	gedcomx::Citation c;
	c.id = "C_1";
	c.sourceId = "S1";
	c.page = "p. 12";
	c.date.original = "1 June 1900";
	c.date.formal = "+1900-06-01";
	c.confidence = "http://gedcomx.org/High";
	c.notes.push_back({ "en", "", "checked twice" });

	gedcomx::Citation bare;  // never exported as a description
	bare.id = gedcomx::bareCitationId("S1");
	bare.sourceId = "S1";

	auto doc = gedcomx::serializeSourceExport(source, {}, {}, { other }, { c, bare });

	auto sources = gedcomx::parseSourceDescriptions(doc);
	REQUIRE(sources.size() == 2);  // S1 and S2; the citation is not a source
	CHECK(sources[0].id == "S1");
	CHECK(sources[1].id == "S2");

	auto citations = gedcomx::parseCitations(doc);
	REQUIRE(citations.size() == 1);
	CHECK(citations[0].id == "C_1");
	CHECK(citations[0].sourceId == "S1");
	CHECK(citations[0].page == "p. 12");
	CHECK(citations[0].date.original == "1 June 1900");
	CHECK(citations[0].date.formal == "+1900-06-01");
	CHECK(citations[0].confidence == "http://gedcomx.org/High");
	REQUIRE(citations[0].notes.size() == 1);
	CHECK(citations[0].notes[0].text == "checked twice");
}

TEST_CASE("A reference to a citation resoles after import; a reference to a source stays bare") {
	auto json = nlohmann::json::parse(R"({
		"sourceDescriptions": [
			{ "id": "S1", "titles": [{ "value": "Census" }] },
			{ "id": "C_1", "componentOf": { "description": "#S1" }, "citations": [{	"value": "p. 12" }] }
		],
		"persons": [ { "id": "P1", "sources": [
			{ "description": { "resource": "#C_1" } },
			{ "description": { "resource": "#S1" } } ] } ]
	})");

	auto persons = gedcomx::parsePersons(json);
	auto citations = gedcomx::parseCitations(json);
	std::vector<gedcomx::Relationship> relationships;
	gedcomx::resolveCitationReferences(persons, relationships, citations);

	REQUIRE(persons.size() == 1);
	REQUIRE(persons[0].conclusion.sources.size() == 2);
	CHECK(persons[0].conclusion.sources[0].citationId == "C_1");  // the citation
	CHECK(persons[0].conclusion.sources[1].citationId == gedcomx::bareCitationId("S1"));  // the whole source
}

TEST_CASE("Nexted components flatten to the root source; a component of a missing parent stays a source") {
	auto json = nlohmann::json::parse(R"({ "sourceDescriptions": [
		{ "id": "S1" },
		{ "id": "A", "componentOf": { "description": "#S1" } },
		{ "id": "B", "componentOf": { "description": "#A" } },
		{ "id": "X", "componentOf": { "description": "#NOT_IN_FILE" } }
	] })");

	auto sources = gedcomx::parseSourceDescriptions(json);
	REQUIRE(sources.size() == 2);
	CHECK(sources[0].id == "S1");
	CHECK(sources[1].id == "X");

	auto citations = gedcomx::parseCitations(json);
	REQUIRE(citations.size() == 2);
	CHECK(citations[0].sourceId == "S1");  // A
	CHECK(citations[1].sourceId == "S1");  // B, through A
}