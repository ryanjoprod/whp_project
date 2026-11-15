#include <doctest/doctest.h>

#include <stdexcept>

#include "test_helpers.h"
#include "storage/database.h"
#include "gedcomx/person.h"

TEST_CASE("Person round-trips through storage correctly") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person person;
	person.id = "P1";
	person.conclusion.confidence = "http://gedcomx.org/High";
	person.conclusion.lang = "en";
	person.conclusion.notes.push_back({ "en", "", "Test note." });

	gedcomx::Name name;
	gedcomx::NameForm form;
	form.fullText = "George Washington";
	form.parts.push_back({ "George", "http://gedcomx.org/Given" });
	form.parts.push_back({ "Washington", "http://gedcomx.org/Surname" });
	name.nameForms.push_back(form);
	person.names.push_back(name);

	gedcomx::Fact fact;
	fact.id = "F1";
	fact.type = "http://gedcomx.org/Birth";
	fact.date.original = "22 February 1732";
	fact.conclusion.confidence = "http://gedcomx.org/Medium";
	person.facts.push_back(fact);

	db.save(person, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());

	CHECK(loaded->id == "S1:P1");
	CHECK(loaded->sourceId == "S1");
	CHECK(loaded->primaryFullText() == "George Washington");
	CHECK(loaded->conclusion.confidence == "http://gedcomx.org/High");
	CHECK(loaded->conclusion.lang == "en");
	REQUIRE(loaded->conclusion.notes.size() == 1);
	CHECK(loaded->conclusion.notes[0].text == "Test note.");

	REQUIRE(loaded->facts.size() == 1);
	CHECK(loaded->facts[0].id == "F1");
	CHECK(loaded->facts[0].type == "http://gedcomx.org/Birth");
	CHECK(loaded->facts[0].date.original == "22 February 1732");
	CHECK(loaded->facts[0].conclusion.confidence == "http://gedcomx.org/Medium");
}

TEST_CASE("Re-saving a person upserts rather than duplicates") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person person;
	person.id = "P1";
	person.names.push_back({});
	person.names[0].nameForms.push_back({ "Original Name", {} });

	db.save(person, "S1");

	person.names[0].nameForms[0].fullText = "Updated Name";
	db.save(person, "S1");   // save again — should replace, not duplicate

	auto all = db.loadAll();
	REQUIRE(all.size() == 1);   // still just one person, not two
	CHECK(all[0].primaryFullText() == "Updated Name");
}

TEST_CASE("Adding a fact via re-save preserves existing facts") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person person;
	person.id = "P1";
	gedcomx::Fact birthFact;
	birthFact.id = "F1";
	birthFact.type = "http://gedcomx.org/Birth";
	person.facts.push_back(birthFact);

	db.save(person, "S1");

	// Simulate the "Add Fact" flow: load back, append a new fact, save again.
	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());

	gedcomx::Person updated = *loaded;
	updated.id = "P1";  // strip row-key back to local id before saving, same as the UI does
	gedcomx::Fact deathFact;
	deathFact.id = "F2";
	deathFact.type = "http://gedcomx.org/Death";
	updated.facts.push_back(deathFact);

	db.save(updated, "S1");

	auto reloaded = db.loadPerson("S1:P1");
	REQUIRE(reloaded.has_value());
	REQUIRE(reloaded->facts.size() == 2);  // both facts present, neither lost
}

TEST_CASE("Person round-trip: gender, extended name fields, attributes, addresses") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person p;
	p.id = "P1";
	p.sourceId = "S1";
	p.gender = gedcomx::Gender{ "http://gedcomx.org/Male" };

	gedcomx::Name name;
	name.title = "Dr.";
	name.nickName = "Bud";
	name.callName = "Bud";
	p.names.push_back(name);

	p.attributes.push_back({ "AT1", "Occupation", "Carpenter" });
	p.addresses.push_back({ "AD1", "", "Springfield", "IL", "USA", "62704" });

	db.save(p, "S1");
	auto loaded = db.loadPerson("S1:P1");

	REQUIRE(loaded.has_value());
	CHECK(loaded->gender.has_value());
	CHECK(loaded->gender->type == "http://gedcomx.org/Male");
	REQUIRE(loaded->names.size() == 1);
	CHECK(loaded->names[0].title == "Dr.");
	CHECK(loaded->names[0].nickName == "Bud");
	REQUIRE(loaded->attributes.size() == 1);
	CHECK(loaded->attributes[0].type == "Occupation");
	REQUIRE(loaded->addresses.size() == 1);
	CHECK(loaded->addresses[0].city == "Springfield");
}

TEST_CASE("Resaving a person does not leave stale facts behind") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person p1;
	p1.id = "P1";

	gedcomx::Fact birth;
	birth.id = "F1";
	birth.type = "http://gedcomx.org/Birth";
	gedcomx::Fact death;
	death.id = "F2";
	death.type = "http://gedcomx.org/Death";
	p1.facts = { birth, death };
	db.save(p1, "S1");

	auto before = db.loadPerson("S1:P1");
	REQUIRE(before.has_value());
	REQUIRE(before->facts.size() == 2);

	p1.facts.pop_back();
	db.save(p1, "S1");

	auto after = db.loadPerson("S1:P1");
	REQUIRE(after.has_value());
	REQUIRE(after->facts.size() == 1);
	CHECK(after->facts[0].type == "http://gedcomx.org/Birth");
}

TEST_CASE("Facts without ids are all kept") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person p;
	p.id = "P1";
	gedcomx::Fact a;
	a.type = "http://gedcomx.org/Birth";  // no id
	gedcomx::Fact b;
	b.type = "http://gedcomx.org/Death";  // no id
	p.facts = { a, b };
	db.save(p, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->facts.size() == 2);
}

TEST_CASE("A failed save does not poison the connection") {
	storage::Database db(":memory:");

	gedcomx::Person p;
	p.id = "P1";

	CHECK_THROWS_AS(db.save(p, "NO_SUCH_SOURCE"), std::runtime_error);

	testutil::saveSource(db, "S1");
	CHECK_NOTHROW(db.save(p, "S1"));  // would fail with "transaction within a transaction" without the guard
	CHECK(db.loadPerson("S1:P1").has_value());
}

TEST_CASE("runInTransaction rolls back everything when the work throws") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Person p;
	p.id = "P1";

	CHECK_THROWS(db.runInTransaction([&] {
		db.save(p, "S1");  // nexted save inside the outer transaction
		throw std::runtime_error("boom");
	}));
	CHECK_FALSE(db.loadPerson("S1:P1").has_value());

	CHECK_NOTHROW(db.save(p, "S1"));  // connection still healthy
}

TEST_CASE("Deleting a person removes their relationships and links, not their partners") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	testutil::saveSourceWithCitation(db, "SD1");

	for (const char* id : { "P1", "P2", "P3" }) {
		gedcomx::Person p;
		p.id = id;
		db.save(p, "S1");
	}
	gedcomx::Person twin;
	twin.id = "P1";
	db.save(twin, "S2");
	db.linkPersons("AGG1", { { "S1", "P1" },{ "S2", "P1" } });

	gedcomx::Relationship couple;
	couple.id = "R1";
	couple.type = "http://gedcomx.org/Couple";
	couple.person1Id = "P1";
	couple.person2Id = "P2";
	gedcomx::Fact marriage;
	marriage.id = "F1";
	marriage.type = "http://gedcomx.org/Marriage";
	marriage.conclusion.sources.push_back({ gedcomx::bareCitationId("SD1") });
	couple.facts.push_back(marriage);

	gedcomx::Relationship child;
	child.id = "R2";
	child.type = "http://gedcomx.org/ParentChild";
	child.person1Id = "P1";
	child.person2Id = "P3";
	db.saveAll({ couple, child }, "S1");

	CHECK(db.deletePerson("S1:P1"));

	CHECK_FALSE(db.loadPerson("S1:P1").has_value());
	CHECK(db.loadPerson("S1:P2").has_value());  // partner survives
	CHECK(db.loadPerson("S1:P3").has_value());  // child survives
	CHECK(db.loadRelationshipsForPerson("S1:P2").empty());  // couple went with P1
	CHECK(db.loadRelationshipsForPerson("S1:P3").empty());  // parent-child went too
	CHECK(db.getLinkedPersonKeys("S2:P1").empty());  // link row removed
}

TEST_CASE("Apostrophes in ids and names do not break storage") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S'1");

	gedcomx::Person p1;
	p1.id = "P'1";
	gedcomx::Name name;
	gedcomx::NameForm form;
	form.fullText = "Jo D'Angelo";
	name.nameForms.push_back(form);
	p1.names.push_back(name);
	gedcomx::Fact fact;
	fact.id = "F'1";
	fact.type = "http://gedcomx.org/Birth";
	p1.facts.push_back(fact);

	gedcomx::Person p2;
	p2.id = "P'2";

	CHECK_NOTHROW(db.save(p1, "S'1"));
	CHECK_NOTHROW(db.save(p1, "S'1"));  // second save exercises the DELETE-then-reinsert paths
	CHECK_NOTHROW(db.save(p2, "S'1"));

	gedcomx::Relationship rel;
	rel.id = "R'1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = p1.id;
	rel.person2Id = p2.id;
	CHECK_NOTHROW(db.save(rel, "S'1"));
	CHECK_NOTHROW(db.save(rel, "S'1"));

	auto loaded = db.loadPerson("S'1:P'1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->primaryFullText() == "Jo D'Angelo");
	CHECK(db.loadRelationshipsForPerson("S'1:P'1").size() == 1);
	CHECK(db.deletePerson("S'1:P'1"));
}

TEST_CASE("A person's own citations are saved and loaded") {
	storage::Database db(":memory:");

	auto cite = testutil::saveSourceWithCitation(db, "SD1");
	testutil::saveSource(db, "S1");

	gedcomx::Person p;
	p.id = "P1";
	p.conclusion.sources.push_back({ cite });
	db.save(p, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->conclusion.sources.size() == 1);
	CHECK(loaded->conclusion.sources[0].citationId == cite);

	p.conclusion.sources.clear();  // user removes it in the editor
	db.save(p, "S1");
	CHECK(db.loadPerson("S1:P1")->conclusion.sources.empty());
}

TEST_CASE("A person fact's citations are saved and loaded") {
	storage::Database db(":memory:");

	auto cite = testutil::saveSourceWithCitation(db, "SD1");
	testutil::saveSource(db, "S1");

	gedcomx::Person p;
	p.id = "P1";
	gedcomx::Fact birth;
	birth.id = "F1";
	birth.type = "http://gedcomx.org/Birth";
	birth.conclusion.sources.push_back({ cite });
	p.facts.push_back(birth);
	db.save(p, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->facts.size() == 1);
	REQUIRE(loaded->facts[0].conclusion.sources.size() == 1);
	CHECK(loaded->facts[0].conclusion.sources[0].citationId == cite);
}

TEST_CASE("A person citing a citation that does not exist is rejected, person and all") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Person p;
	p.id = "P1";
	p.conclusion.sources.push_back({ "NO_SUCH_CITATION" });

	CHECK_THROWS_AS(db.save(p, "S1"), std::runtime_error);
	CHECK_FALSE(db.loadPerson("S1:P1").has_value());
}

TEST_CASE("A person fact's calendar, quality and sort value round-trip") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Person person;
	person.id = "P1";

	gedcomx::Fact fact;
	fact.id = "F1";
	fact.type = "http://gedcomx.org/Birth";
	fact.place.original = "Jerusalem";
	fact.value = "a value";
	fact.conclusion.confidence = "http://gedcomx.org/High";
	fact.date = testutil::makeHebrewDate();
	person.facts.push_back(fact);

	gedcomx::Fact plain;  // a fact with no date at all
	plain.id = "F2";
	plain.type = "http://gedcomx.org/Death";
	person.facts.push_back(plain);
	db.save(person, "S1");

	auto loaded = db.loadPerson("S1:P1");
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->facts.size() == 2);

	testutil::checkHebrewDate(loaded->facts[0].date);
	CHECK(loaded->facts[0].place.original == "Jerusalem");  // columns around the date still line up
	CHECK(loaded->facts[0].value == "a value");
	CHECK(loaded->facts[0].conclusion.confidence == "http://gedcomx.org/High");

	CHECK(loaded->facts[1].date.calendar == "");
	CHECK_FALSE(loaded->facts[1].date.sortValue.has_value());
}