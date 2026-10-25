#include <doctest/doctest.h>

#include "storage/database.h"
#include "gedcomx/person.h"

TEST_CASE("Person round-trips through storage correctly") {
	storage::Database db(":memory:");

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