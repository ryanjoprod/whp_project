#include <doctest/doctest.h>

#include "gedcomx/parser.h"

TEST_CASE("parsePerson extracts id and primary name") {
	auto j = nlohmann::json::parse(R"({
        "id": "P1",
        "names": [
            {
                "nameForms": [
                    {
                        "fullText": "George Washington",
                        "parts": [
                            { "value": "George", "type": "http://gedcomx.org/Given" },
                            { "value": "Washington", "type": "http://gedcomx.org/Surname" }
                        ]
                    }
                ]
            }
        ]
    })");

	gedcomx::Person person = gedcomx::parsePerson(j);

	CHECK(person.id == "P1");
	CHECK(person.primaryFullText() == "George Washington");
	REQUIRE(person.names.size() == 1);
	REQUIRE(person.names[0].nameForms.size() == 1);
	REQUIRE(person.names[0].nameForms[0].parts.size() == 2);
	CHECK(person.names[0].nameForms[0].parts[0].value == "George");
	CHECK(person.names[0].nameForms[0].parts[0].type == "http://gedcomx.org/Given");
}

TEST_CASE("parsePerson handles missing optional fields gracefully") {
	nlohmann::json j = { {"id", "P2"} };  // no names, no facts, no conclusion fields

	gedcomx::Person person = gedcomx::parsePerson(j);

	CHECK(person.id == "P2");
	CHECK(person.primaryFullText() == "");
	CHECK(person.names.empty());
	CHECK(person.facts.empty());
	CHECK(person.conclusion.confidence == "");
}

TEST_CASE("parsePersons reads multiple persons from a document") {
    auto doc = nlohmann::json::parse("{\"persons\":[{\"id\":\"P1\"},{\"id\":\"P2\"}]}");

    auto people = gedcomx::parsePersons(doc);

    REQUIRE(people.size() == 2);
    CHECK(people[0].id == "P1");
    CHECK(people[1].id == "P2");
}