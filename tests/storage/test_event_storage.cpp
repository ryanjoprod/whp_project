#include <doctest/doctest.h>

#include <set>

#include "test_helpers.h"

#include "storage/database.h"
#include "gedcomx/citation.h"

namespace {
	gedcomx::Event makeEvent(const std::string& id) {
		gedcomx::Event e;
		e.id = id;
		e.type = "http://gedcomx.org/Baptism";
		return e;
	}

	void savePerson(storage::Database& db, const std::string& id, const std::string& sourceId) {
		gedcomx::Person p;
		p.id = id;
		db.save(p, sourceId);
	}

	gedcomx::EventRole makeRole(const std::string& personId, const std::string& type) {
		gedcomx::EventRole r;
		r.personId = personId;
		r.type = type;
		return r;
	}
}

TEST_CASE("An event round-trips with date, place, description, roles and notes") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	savePerson(db, "P1", "S1");
	savePerson(db, "P2", "S1");

	gedcomx::Event e = makeEvent("E1");
	e.date = testutil::makeHebrewDate();
	e.place.original = "Jerusalem";
	e.place.descriptionId = "PL1";
	e.description = "Naming ceremony";
	e.conclusion.confidence = "http://gedcomx.org/High";
	e.conclusion.notes.push_back({ "en", "", "Event note" });

	gedcomx::EventRole principal = makeRole("P1", "http://gedcomx.org/Principal");
	principal.details = "the child";
	principal.conclusion.notes.push_back({ "en", "", "Role note" });
	e.roles.push_back(principal);
	e.roles.push_back(makeRole("P2", "http://gedcomx.org/Witness"));
	db.save(e, "S1");

	auto events = db.loadAllEvents();
	REQUIRE(events.size() == 1);
	const auto& loaded = events[0];
	CHECK(loaded.id == "S1:E1");
	CHECK(loaded.sourceId == "S1");
	CHECK(loaded.type == "http://gedcomx.org/Baptism");
	testutil::checkHebrewDate(loaded.date);
	CHECK(loaded.place.original == "Jerusalem");
	CHECK(loaded.place.descriptionId == "PL1");
	CHECK(loaded.description == "Naming ceremony");
	CHECK(loaded.conclusion.confidence == "http://gedcomx.org/High");
	REQUIRE(loaded.conclusion.notes.size() == 1);
	CHECK(loaded.conclusion.notes[0].text == "Event note");

	REQUIRE(loaded.roles.size() == 2);
	CHECK(loaded.roles[0].personId == "S1:P1");
	CHECK(loaded.roles[0].details == "the child");
	REQUIRE(loaded.roles[0].conclusion.notes.size() == 1);
	CHECK(loaded.roles[0].conclusion.notes[0].text == "Role note");
	CHECK(loaded.roles[1].personId == "S1:P2");
}

TEST_CASE("The same local event id in two sources does not collide") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");

	gedcomx::Event a = makeEvent("E1");
	a.description = "from S1";
	gedcomx::Event b = makeEvent("E1");
	b.description = "from S2";
	db.save(a, "S1");
	db.save(b, "S2");

	auto events = db.loadAllEvents();
	REQUIRE(events.size() == 2);
	std::set<std::string> ids;
	for (const auto& e : events)
		ids.insert(e.id);
	std::set<std::string> expected = { "S1:E1", "S2:E1" };
	CHECK(ids == expected);

	auto onlyS2 = db.loadEventsForSource("S2");
	REQUIRE(onlyS2.size() == 1);
	CHECK(onlyS2[0].description == "from S2");
}

TEST_CASE("Re-saving an event replaces its roles") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	savePerson(db, "P1", "S1");
	savePerson(db, "P2", "S1");

	gedcomx::Event e = makeEvent("E1");
	e.roles.push_back(makeRole("P1", "http://gedcomx.org/Principal"));
	e.roles.push_back(makeRole("P2", "http://gedcomx.org/Witness"));
	db.save(e, "S1");

	e.roles.pop_back();
	db.save(e, "S1");

	auto events = db.loadAllEvents();
	REQUIRE(events.size() == 1);
	REQUIRE(events[0].roles.size() == 1);
	CHECK(events[0].roles[0].personId == "S1:P1");
}

TEST_CASE("A role naming a person who does not exist is rejected and nothing is saved") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Event e = makeEvent("E1");
	e.roles.push_back(makeRole("NOBODY", "http://gedcomx.org/Principal"));

	CHECK_THROWS(db.save(e, "S1"));
	CHECK(db.loadAllEvents().empty());  // the transaction rolled the event back too
}

TEST_CASE("Deleting a person removes their role but not the event") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	savePerson(db, "P1", "S1");
	savePerson(db, "P2", "S1");

	gedcomx::Event e = makeEvent("E1");
	e.roles.push_back(makeRole("P1", "http://gedcomx.org/Principal"));
	e.roles.push_back(makeRole("P2", "http://gedcomx.org/Witness"));
	db.save(e, "S1");

	db.deletePerson("S1:P1");

	auto events = db.loadAllEvents();
	REQUIRE(events.size() == 1);
	REQUIRE(events[0].roles.size() == 1);
	CHECK(events[0].roles[0].personId == "S1:P2");
}

TEST_CASE("Event and role citations round-trip") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	savePerson(db, "P1", "S1");
	testutil::saveSourceWithCitation(db, "SD1");
	testutil::saveSourceWithCitation(db, "SD2");

	gedcomx::Event e = makeEvent("E1");
	e.conclusion.sources.push_back({ gedcomx::bareCitationId("SD1") });
	gedcomx::EventRole role = makeRole("P1", "http://gedcomx.org/Principal");
	role.conclusion.sources.push_back({ gedcomx::bareCitationId("SD2") });
	e.roles.push_back(role);
	db.save(e, "S1");

	auto events = db.loadAllEvents();
	REQUIRE(events.size() == 1);
	REQUIRE(events[0].conclusion.sources.size() == 1);
	CHECK(events[0].conclusion.sources[0].citationId == gedcomx::bareCitationId("SD1"));
	REQUIRE(events[0].roles.size() == 1);
	REQUIRE(events[0].roles[0].conclusion.sources.size() == 1);
	CHECK(events[0].roles[0].conclusion.sources[0].citationId == gedcomx::bareCitationId("SD2"));
}