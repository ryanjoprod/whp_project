#include <doctest/doctest.h>

#include <stdexcept>

#include "test_helpers.h"
#include "storage/database.h"
#include "gedcomx/relationship.h"

TEST_CASE("Relationship round-trip through storage correctly") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	
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
	person2.conclusion.confidence = "http://gedcomx.org/Medium";
	person2.conclusion.lang = "en";
	person2.conclusion.notes.push_back({ "en", "", "Test note 2" });

	gedcomx::Name name2;
	gedcomx::NameForm form2;
	form2.fullText = "Eve";
	form2.parts.push_back({ "Eve", "http://gedcomx.org/Given" });
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

TEST_CASE("Relationship sources round-trip through storage correctly") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "SD1");

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

	testutil::saveSource(db, "S1");

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

	auto p3Rels = db.loadRelationshipsForPerson("S1:P3");
	REQUIRE(p3Rels.size() == 2);			// R2 and R3: proves the whole batch was written
	CHECK(p3Rels[0].person1Id == "S1:P1");	// ordered by row_key: R2, then R3
	CHECK(p3Rels[1].person1Id == "S1:P2");
}

TEST_CASE("Round-trip edits don't contain stale facts") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

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

TEST_CASE("Resaving a relationship replaces its citations") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "SD1");
	testutil::saveSource(db, "SD2");

	gedcomx::Person p1;
	p1.id = "P1";
	gedcomx::Person p2;
	p2.id = "P2";
	db.save(p1, "S1");
	db.save(p2, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = "P1";
	rel.person2Id = "P2";
	rel.conclusion.sources = { { "SD1" },{ "SD2" } };
	db.save(rel, "S1");

	REQUIRE(db.loadRelationshipsForPerson("S1:P1")[0].conclusion.sources.size() == 2);

	rel.conclusion.sources.pop_back();  // user removes a citation in the editor
	db.save(rel, "S1");

	auto after = db.loadRelationshipsForPerson("S1:P1");
	REQUIRE(after.size() == 1);
	REQUIRE(after[0].conclusion.sources.size() == 1);
	CHECK(after[0].conclusion.sources[0].sourceDescriptionId == "SD1");
}

TEST_CASE("saveAll rolls back the whole batch when one relationship is invalid") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person p1;
	p1.id = "P1";
	gedcomx::Person p2;
	p2.id = "P2";
	db.save(p1, "S1");
	db.save(p2, "S1");

	gedcomx::Relationship good;
	good.id = "R1";
	good.type = "http://gedcomx.org/Couple";
	good.person1Id = "P1";
	good.person2Id = "P2";

	gedcomx::Relationship bad;
	bad.id = "R2";
	bad.type = "http://gedcomx.org/Couple";
	bad.person1Id = "P1";
	bad.person2Id = "P99";  // no such person

	CHECK_THROWS_AS(db.saveAll({ good, bad }, "S1"), std::runtime_error);
	CHECK(db.loadRelationshipsForPerson("S1:P1").empty());	// 'good' was rolled back, too
}

TEST_CASE("Deleting a relationship leaves both people") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person p1;
	p1.id = "P1";
	db.save(p1, "S1");
	gedcomx::Person p2;
	p2.id = "P2";
	db.save(p2, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = "P1";
	rel.person2Id = "P2";
	db.save(rel, "S1");

	CHECK(db.deleteRelationship("S1:R1"));
	CHECK(db.loadPerson("S1:P1").has_value());
	CHECK(db.loadPerson("S1:P2").has_value());
	CHECK(db.loadRelationshipsForPerson("S1:P1").empty());
	CHECK_FALSE(db.deleteRelationship("S1:R1"));  // already gone;
	CHECK_FALSE(db.deletePerson("S1:NOBODY"));
}

TEST_CASE("Re-saving a person keeps their relationships") {
	// Guards the upsert change: INSERT OR REPLACE would have cascaded and deleted these.
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person p1;
	p1.id = "P1";
	db.save(p1, "S1");
	gedcomx::Person p2;
	p2.id = "P2";
	db.save(p2, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = "P1";
	rel.person2Id = "P2";
	db.save(rel, "S1");

	p1.gender = gedcomx::Gender{ "http://gedcomx.org/Male" };  // an "edit"
	db.save(p1, "S1");

	CHECK(db.loadRelationshipsForPerson("S1:P1").size() == 1);
	CHECK(db.loadPerson("S1:P1")->gender.has_value());
}