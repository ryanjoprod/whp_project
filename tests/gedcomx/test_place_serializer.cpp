#include <doctest/doctest.h>

#include <string>

#include <nlohmann/json.hpp>

#include "gedcomx/parser.h"
#include "gedcomx/citation.h"

TEST_CASE("A place reads dated names, code, urls, and dated jurisdictions") {
	auto j = nlohmann::json::parse(R"({
		"id": "PL1",
		"type": "http://gedcomx.org/City",
		"code": "ZIP 22030",
		"names": [
			{ "lang": "en", "value": "Constantinople", "date": { "original": "before 1930" } },
			{ "lang": "en", "value": "Istanbul" }
		],
		"latitude": 41.0,
		"longitude": 28.9,
		"jurisdictions": [
			{ "resource": "#PL2", "date": { "original": "1453-1922" } },
			{ "resource": "#PL3" }
		],
		"urls": [ { "url": "https://example.org", "description": "Info", "type": "web" } ]
	})");

	auto place = gedcomx::parsePlaceDescription(j);

	CHECK(place.code == "ZIP 22030");
	REQUIRE(place.names.size() == 2);
	CHECK(place.names[0].date.original == "before 1930");
	CHECK(place.names[1].date.original.empty());
	REQUIRE(place.latitude.has_value());
	CHECK(*place.latitude == doctest::Approx(41.0));

	REQUIRE(place.jurisdictions.size() == 2);
	CHECK(place.jurisdictions[0].parentId == "PL2");
	CHECK(place.jurisdictions[0].date.original == "1453-1922");
	CHECK(place.jurisdictions[1].parentId == "PL3");

	REQUIRE(place.urls.size() == 1);
	CHECK(place.urls[0].url == "https://example.org");
	CHECK(place.urls[0].type == "web");
}

TEST_CASE("A standard single jurisdiction still reads") {
	auto j = nlohmann::json::parse(R"({ "id": "PL1", "jurisdiction": { "resource": "#PL2" } })");

	auto place = gedcomx::parsePlaceDescription(j);

	REQUIRE(place.jurisdictions.size() == 1);
	CHECK(place.jurisdictions[0].parentId == "PL2");
	CHECK(place.jurisdictions[0].date.original.empty());
}

TEST_CASE("A plain single parent writes only the standard jurisdiction member") {
	gedcomx::PlaceDescription place;
	place.id = "PL1";
	place.jurisdictions.push_back({ "PL2",{} });

	auto j = gedcomx::serializePlaceDescription(place);

	REQUIRE(j.contains("jurisdiction"));
	CHECK(j.at("jurisdiction").at("resource").get<std::string>() == "#PL2");
	CHECK_FALSE(j.contains("jurisdictions"));
}

TEST_CASE("Dated or multiple parents also writ the jurisdictions list, first parent standard") {
	gedcomx::PlaceDescription place;
	place.id = "PL1";
	gedcomx::Date d;
	d.original = "1453-1922";
	place.jurisdictions.push_back({ "PL2", d });
	place.jurisdictions.push_back({ "PL3",{} });

	auto j = gedcomx::serializePlaceDescription(place);

	CHECK(j.at("jurisdiction").at("resource").get<std::string>() == "#PL2");
	REQUIRE(j.contains("jurisdictions"));
	REQUIRE(j.at("jurisdictions").size() == 2);
	CHECK(j.at("jurisdictions")[0].at("date").at("original").get<std::string>() == "1453-1922");
}

TEST_CASE("A place with nothing but an id writes nothing else") {
	gedcomx::PlaceDescription place;
	place.id = "PL1";

	auto j = gedcomx::serializePlaceDescription(place);

	CHECK(j.at("id").get<std::string>() == "PL1");
	CHECK_FALSE(j.contains("type"));
	CHECK_FALSE(j.contains("code"));
	CHECK_FALSE(j.contains("names"));
	CHECK_FALSE(j.contains("latitude"));
	CHECK_FALSE(j.contains("jurisdiction"));
	CHECK_FALSE(j.contains("urls"));
}

TEST_CASE("A place round-trips through serialize and parse") {
	gedcomx::PlaceDescription place;
	place.id = "PL1";
	place.type = "http://gedcomx.org/City";
	place.code = "34000";
	place.latitude = 41.0;
	place.longitude = 28.9;

	gedcomx::PlaceName old;
	old.lang = "en";
	old.value = "Constantinople";
	old.date.original = "before 1930";
	place.names.push_back(old);
	place.names.push_back({ "tr", "Istanbul",{} });

	gedcomx::Date d;
	d.original = "1453-1922";
	d.sortValue = 2250000;
	place.jurisdictions.push_back({ "PL2", d });
	place.jurisdictions.push_back({ "PL3",{} });
	place.urls.push_back({ "https://example.org", "Info", "web" });

	place.conclusion.confidence = "http://gedcomx.org/High";
	place.conclusion.notes.push_back({ "en", "Subject", "A note" });
	place.conclusion.sources.push_back({ gedcomx::bareCitationId("S1") });

	auto back = gedcomx::parsePlaceDescription(gedcomx::serializePlaceDescription(place));

	CHECK(back.id == "PL1");
	CHECK(back.type == "http://gedcomx.org/City");
	CHECK(back.code == "34000");
	REQUIRE(back.latitude.has_value());
	CHECK(*back.latitude == doctest::Approx(41.0));

	REQUIRE(back.names.size() == 2);
	CHECK(back.names[0].value == "Constantinople");
	CHECK(back.names[0].date.original == "before 1930");
	CHECK(back.names[1].lang == "tr");

	REQUIRE(back.jurisdictions.size() == 2);
	CHECK(back.jurisdictions[0].parentId == "PL2");
	CHECK(back.jurisdictions[0].date.original == "1453-1922");
	REQUIRE(back.jurisdictions[0].date.sortValue.has_value());
	CHECK(*back.jurisdictions[0].date.sortValue == 2250000);
	CHECK(back.jurisdictions[1].parentId == "PL3");

	REQUIRE(back.urls.size() == 1);
	CHECK(back.urls[0].description == "Info");

	CHECK(back.conclusion.confidence == "http://gedcomx.org/High");
	REQUIRE(back.conclusion.notes.size() == 1);
	CHECK(back.conclusion.notes[0].text == "A note");
	REQUIRE(back.conclusion.sources.size() == 1);
	CHECK(back.conclusion.sources[0].citationId == gedcomx::bareCitationId("S1"));
}

TEST_CASE("A source export writes its places, and none when there are none") {
	gedcomx::SourceDescription src;
	src.id = "S1";
	std::vector<gedcomx::PlaceDescription> places(1);
	places[0].id = "PL1";

	auto with = gedcomx::serializeSourceExport(src, {}, {}, {}, {}, {}, places);
	REQUIRE(with.contains("places"));
	CHECK(with.at("places").size() == 1);

	auto without = gedcomx::serializeSourceExport(src, {}, {}, {}, {}, {});
	CHECK_FALSE(without.contains("places"));
}

TEST_CASE("A place's reference to a real citation points at that citation") {
	gedcomx::Citation c;
	c.id = "C1";
	c.sourceId = "S1";

	std::vector<gedcomx::Person> people;
	std::vector<gedcomx::Relationship> rels;
	std::vector<gedcomx::Event> events;
	std::vector<gedcomx::PlaceDescription> places(1);
	places[0].conclusion.sources.push_back({ gedcomx::bareCitationId("C1") });

	gedcomx::resolveCitationReferences(people, rels, events, places, { c });

	CHECK(places[0].conclusion.sources[0].citationId == "C1");
}