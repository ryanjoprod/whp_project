#include <doctest/doctest.h>

#include "storage/database.h"
#include "gedcomx/relationship.h"

TEST_CASE("Relationship round-trip through storage correctly") {
	storage::Database db(":memory:");
	
	gedcomx::Person person1;
	person1.id = "P1";
	person1.conclusion.confidence = "http://gedcomx.org/High";
	person1.conclusion.lang = "en";
	person1.conclusion.notes.push_back({ "en", "", "Test note 1" });

	gedcomx::Name name1;
	gedcomx::NameForm form1;
	form1.fullText = "Adam";
	form1.parts.push_back({ "Adam", "http://gedcomx.org/Given" });
	name1.nameForms.push_back(form1);
	person1.names.push_back(name1);

	db.save(person1, "S1");

	gedcomx::Person person2;
	person2.id = "P2";
	person2.conclusion.confidence = "http://gedcomx.org/Given";
	person2.conclusion.lang = "en";
	person2.conclusion.notes.push_back({ "en", "", "Test note 2" });

	gedcomx::Name name2;
	gedcomx::NameForm form2;
	form2.fullText = "Eve";
	form2.parts.push_back({ "Eve", "http://gedcomx.org/High" });
	name2.nameForms.push_back(form2);
	person2.names.push_back(name2);

	db.save(person2, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = "P1";
	rel.person2Id = "P2";

	gedcomx::Fact fact;
	fact.id = "F1";
	fact.type = "http://gedcomx.org/Marriage";
	rel.facts.push_back(fact);

	// save() takes local ids and prefixes the source; load returns row keys.
	db.save(rel, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());

	CHECK(loaded->id == "S1:P1");
	CHECK(loaded->primaryFullText() == "Adam");

	auto loadedRels = db.loadRelationshipsForPerson("S1:P1");
	REQUIRE(loadedRels.size() == 1);
	CHECK(loadedRels[0].type == "http://gedcomx.org/Couple");
	CHECK(loadedRels[0].person1Id == "S1:P1");
	CHECK(loadedRels[0].person2Id == "S1:P2");
	CHECK(loadedRels[0].facts[0].type == "http://gedcomx.org/Marriage");
}

TEST_CASE("Relationship sources round-trip through stoage correctly") {
	storage::Database db(":memory:");

	gedcomx::Person person1;
	person1.id = "P1";
	person1.conclusion.confidence = "http://gedcomx.org/High";
	person1.conclusion.lang = "en";
	person1.conclusion.notes.push_back({ "en", "", "Test note 1" });

	db.save(person1, "S1");

	gedcomx::Person person2;
	person2.id = "P2";
	person2.conclusion.confidence = "http://gedcomx.org/Given";
	person2.conclusion.lang = "en";
	person2.conclusion.notes.push_back({ "en", "", "Test note 2" });

	db.save(person2, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.sourceId = "SD1";
	rel.person1Id = "P1";
	rel.person2Id = "P2";
	rel.conclusion.confidence = "http://gedcomx.org/High";

	gedcomx::Fact fact;
	fact.id = "F1";
	fact.type = "http://gedcomx.org/Marriage";
	rel.facts.push_back(fact);

	gedcomx::SourceReference srcRef;
	srcRef.sourceDescriptionId = "SD1";
	rel.conclusion.sources.push_back(srcRef);

	db.save(rel, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());

	CHECK(loaded->id == "S1:P1");

	auto loadedRels = db.loadRelationshipsForPerson("S1:P1");
	REQUIRE(loadedRels.size() == 1);
	CHECK(loadedRels[0].type == "http://gedcomx.org/Couple");
	CHECK(loadedRels[0].sourceId == "S1");
	CHECK(loadedRels[0].person1Id == "S1:P1");
	CHECK(loadedRels[0].person2Id == "S1:P2");
	CHECK(loadedRels[0].facts[0].type == "http://gedcomx.org/Marriage");
	CHECK(loadedRels[0].conclusion.sources[0].sourceDescriptionId == "SD1");
}

TEST_CASE("Family relationship saveAll() correctly saves all relationships") {
	storage::Database db(":memory:");

	std::vector<gedcomx::Relationship> batch;

	gedcomx::Person p1;
	p1.id = "P1";
	p1.names.push_back({});
	p1.names[0].nameForms.push_back({ "Adam", {} });
	db.save(p1, "S1");

	gedcomx::Person p2;
	p2.id = "P2";
	p2.names.push_back({});
	p2.names[0].nameForms.push_back({ "Eve", {} });
	db.save(p2, "S1");

	gedcomx::Person p3;
	p3.id = "P3";
	p3.names.push_back({});
	p3.names[0].nameForms.push_back({ "Seth", {} });
	db.save(p3, "S1");

	gedcomx::Relationship r1;
	r1.id = "R1";
	r1.type = "http://gedcomx.org/Couple";
	r1.person1Id = "P1";
	r1.person2Id = "P2";
	batch.push_back(r1);

	gedcomx::Relationship r2;
	r2.id = "R2";
	r2.type = "http://gedcomx.org/ParentChild";
	r2.person1Id = "P1";
	r2.person2Id = "P3";
	batch.push_back(r2);

	gedcomx::Relationship r3;
	r3.id = "R3";
	r3.type = "http://gedcomx.org/ParentChild";
	r3.person1Id = "P2";
	r3.person2Id = "P3";
	batch.push_back(r3);

	db.saveAll(batch, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());

	CHECK(loaded->id == "S1:P1");

	auto loadedRels = db.loadRelationshipsForPerson(loaded->id);
	REQUIRE(loadedRels.size() == 2);
	CHECK(loadedRels[0].type == "http://gedcomx.org/Couple");
	CHECK(loadedRels[0].person1Id == "S1:P1");
	CHECK(loadedRels[0].person2Id == "S1:P2");
	CHECK(loadedRels[1].type == "http://gedcomx.org/ParentChild");
	CHECK(loadedRels[1].person1Id == "S1:P1");
	CHECK(loadedRels[1].person2Id == "S1:P3");
}

TEST_CASE("Round-trip edits don't contain stale facts") {
	storage::Database db(":memory:");

	gedcomx::Person p1;
	p1.id = "P1";
	gedcomx::Person p2;
	p2.id = "P2";
	db.save(p1, "S1");
	db.save(p2, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = p1.id;  // local ids go in; row keys come out
	rel.person2Id = p2.id;

	gedcomx::Fact marriage;
	marriage.id = "F1";
	marriage.type = "http://gedcomx.org/Marriage";
	gedcomx::Fact divorce;
	divorce.id = "F2";
	divorce.type = "http://gedcomx.org/Divorce";
	rel.facts = { marriage, divorce };
	db.save(rel, "S1");

	auto before = db.loadRelationshipsForPerson("S1:P1");
	REQUIRE(before.size() == 1);
	REQUIRE(before[0].facts.size() == 2);  // proves the setup

	rel.facts.pop_back();  // simulate removing an event in the editor
	db.save(rel, "S1");

	auto after = db.loadRelationshipsForPerson("S1:P1");
	REQUIRE(after.size() == 1);  // still one relationship, not a duplicate
	REQUIRE(after[0].facts.size() == 1);  // the removed event stays removed
	CHECK(after[0].facts[0].type == "http://gedcomx.org/Marriage");
}