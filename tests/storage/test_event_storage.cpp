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

TEST_CASE("loadEventsForPerson returns the person's events, earliest first, undated last") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	savePerson(db, "P1", "S1");
	savePerson(db, "P2", "S1");

	gedcomx::Event undated = makeEvent("E_NODATE");
	undated.roles.push_back(makeRole("P1", "http://gedcomx.org/Principal"));

	gedcomx::Event late = makeEvent("E_LATE");
	late.date.sortValue = 2415021;
	late.roles.push_back(makeRole("P1", "http://gedcomx.org/Principal"));

	gedcomx::Event early = makeEvent("E_EARLY");
	early.date.sortValue = 2400000;
	early.roles.push_back(makeRole("P1", "http://gedcomx.org/Principal"));
	early.roles.push_back(makeRole("P2", "http://gedcomx.org/Witness"));

	gedcomx::Event other = makeEvent("E_OTHER");
	other.roles.push_back(makeRole("P2", "http://gedcomx.org/Principal"));

	db.save(undated, "S1");
	db.save(late, "S1");
	db.save(early, "S1");
	db.save(other, "S1");

	auto events = db.loadEventsForPerson("S1:P1");
	REQUIRE(events.size() == 3);
	CHECK(events[0].id == "S1:E_EARLY");
	CHECK(events[1].id == "S1:E_LATE");
	CHECK(events[2].id == "S1:E_NODATE");
	CHECK(events[0].roles.size() == 2);  // the whole event, with everyone's role

	CHECK(db.loadEventsForPerson("S1:NOBODY").empty());
}

TEST_CASE("loadEventsForPerson does not mix sources that share a local person id") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	savePerson(db, "P1", "S1");
	savePerson(db, "P1", "S2");

	gedcomx::Event e1 = makeEvent("E1");
	e1.roles.push_back(makeRole("P1", "http://gedcomx.org/Principal"));
	db.save(e1, "S1");

	gedcomx::Event e2 = makeEvent("E2");
	e2.roles.push_back(makeRole("P1", "http://gedcomx.org/Principal"));
	db.save(e2, "S2");

	auto events = db.loadEventsForPerson("S1:P1");
	REQUIRE(events.size() == 1);
	CHECK(events[0].id == "S1:E1");
}

TEST_CASE("deleteEvent removes the event and everything it owns, but not the people") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	savePerson(db, "P1", "S1");

	gedcomx::Event e = makeEvent("E1");
	e.conclusion.notes.push_back({ "en", "", "Event note" });
	gedcomx::EventRole role = makeRole("P1", "http://gedcomx.org/Principal");
	role.conclusion.notes.push_back({ "en", "", "Role note" });
	e.roles.push_back(role);
	db.save(e, "S1");

	db.deleteEvent("S1:E1");
	CHECK(db.loadAllEvents().empty());
	CHECK(db.loadPerson("S1:P1").has_value());
	CHECK_NOTHROW(db.deleteEvent("S1:GONE"));  // nothing there: harmless

	// Saving the same id again starts clean: nothing is left from the deleted event
	db.save(makeEvent("E1"), "S1");
	auto events = db.loadAllEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].roles.empty());
	CHECK(events[0].conclusion.notes.empty());
}