#include <doctest/doctest.h>

#include "gedcomx/parser.h"
#include "test_helpers.h"

TEST_CASE("A date's calendar, quality and sort value survive serrialize/parse on a fact") {
	gedcomx::Fact fact;
	fact.id = "F1";
	fact.type = "http://gedcomx.org/Birth";
	fact.date = testutil::makeHebrewDate();

	nlohmann::json j = gedcomx::serializeFact(fact);
	CHECK(j.at("date").at("calendar") == "Hebrew");
	CHECK(j.at("date").at("sortValue") == 1234567);

	testutil::checkHebrewDate(gedcomx::parseFact(j).date);
}

TEST_CASE("A date from another program still parses, with the new fields blank") {
	auto j = nlohmann::json::parse("{\"id\":\"F1\",\"date\":{\"original\":\"1900\",\"formal\":\"+1900\"}}");
	auto fact = gedcomx::parseFact(j);
	CHECK(fact.date.original == "1900");
	CHECK(fact.date.calendar == "");
	CHECK(fact.date.quality == "");
	CHECK_FALSE(fact.date.sortValue.has_value());
}

TEST_CASE("A fact without a date writes no date key") {
	gedcomx::Fact fact;
	fact.id = "F1";
	CHECK_FALSE(gedcomx::serializeFact(fact).contains("date"));
}

TEST_CASE("A citation's date round-trips through its component description") {
	gedcomx::SourceDescription src;
	src.id = "S1";
	src.titles.push_back({ "",  "Source S1" });

	gedcomx::Citation c;
	c.id = "C1";
	c.sourceId = "S1";
	c.page = "p. 4";
	c.date = testutil::makeHebrewDate();

	nlohmann::json doc;
	doc["sourceDescriptions"] = nlohmann::json::array({ gedcomx::serializeSourceDescription(src), gedcomx::serializeCitation(c) });

	auto parsed = gedcomx::parseCitations(doc);
	REQUIRE(parsed.size() == 1);
	testutil::checkHebrewDate(parsed[0].date);
}

TEST_CASE("An event round-trips through serialize/parse") {
	gedcomx::Event e;
	e.id = "E1";
	e.type = "http://gedcomx.org/Baptism";
	e.date = testutil::makeHebrewDate();
	e.place.original = "Jerusalem";
	e.place.descriptionId = "PL1";
	e.description = "Naming ceremony";
	e.conclusion.confidence = "http://gedcomx.org/High";
	e.conclusion.notes.push_back({ "en", "", "Event note" });
	e.conclusion.sources.push_back({ gedcomx::bareCitationId("S1") });

	gedcomx::EventRole role;
	role.type = "http://gedcomx.org/Principal";
	role.personId = "P1";
	role.details = "the child";
	role.conclusion.notes.push_back({ "en", "", "Role note" });
	e.roles.push_back(role);

	auto r = gedcomx::parseEvent(gedcomx::serializeEvent(e));
	CHECK(r.id == "E1");
	CHECK(r.type == "http://gedcomx.org/Baptism");
	testutil::checkHebrewDate(r.date);
	CHECK(r.place.original == "Jerusalem");
	CHECK(r.place.descriptionId == "PL1");
	CHECK(r.description == "Naming ceremony");
	CHECK(r.conclusion.confidence == "http://gedcomx.org/High");
	REQUIRE(r.conclusion.notes.size() == 1);
	CHECK(r.conclusion.notes[0].text == "Event note");
	REQUIRE(r.conclusion.sources.size() == 1);
	CHECK(r.conclusion.sources[0].citationId == gedcomx::bareCitationId("S1"));

	REQUIRE(r.roles.size() == 1);
	CHECK(r.roles[0].personId == "P1");
	CHECK(r.roles[0].type == "http://gedcomx.org/Principal");
	CHECK(r.roles[0].details == "the child");
	REQUIRE(r.roles[0].conclusion.notes.size() == 1);
	CHECK(r.roles[0].conclusion.notes[0].text == "Role note");
}

TEST_CASE("A bare event serializes without empty keys") {
	gedcomx::Event e;
	e.id = "E1";
	e.type = "http://gedcomx.org/Death";

	nlohmann::json j = gedcomx::serializeEvent(e);
	CHECK_FALSE(j.contains("date"));
	CHECK_FALSE(j.contains("place"));
	CHECK_FALSE(j.contains("description"));
	CHECK_FALSE(j.contains("roles"));
}