#include <doctest/doctest.h>

#include "gedcomx/parser.h"

TEST_CASE("Person round-trips through serialize/parse") {
    auto original = nlohmann::json::parse(
        "{\"id\":\"P1\",\"names\":[{\"nameForms\":[{\"fullText\":\"George Washington\","
        "\"parts\":[{\"value\":\"George\",\"type\":\"http://gedcomx.org/Given\"},"
        "{\"value\":\"Washington\",\"type\":\"http://gedcomx.org/Surname\"}]}]}],"
        "\"facts\":[{\"id\":\"F1\",\"type\":\"http://gedcomx.org/Birth\"}]}");

    gedcomx::Person person = gedcomx::parsePerson(original);
    nlohmann::json serialized = gedcomx::serializePerson(person);
    gedcomx::Person reparsed = gedcomx::parsePerson(serialized);

    CHECK(reparsed.id == person.id);
    CHECK(reparsed.primaryFullText() == person.primaryFullText());
    REQUIRE(reparsed.facts.size() == 1);
    CHECK(reparsed.facts[0].id == "F1");
}

TEST_CASE("A source reference maps to its bare citation and back") {
    auto original = nlohmann::json::parse(
        "{\"id\":\"P1\",\"sources\":[{\"description\":{\"resource\":\"#S1\"}}]}"
    );

    gedcomx::Person person = gedcomx::parsePerson(original);
    REQUIRE(person.conclusion.sources.size() == 1);
    CHECK(person.conclusion.sources[0].citationId == "CB_S1");

    nlohmann::json out = gedcomx::serializePerson(person);
    CHECK(out["sources"][0]["description"]["resource"] == "#S1");  // unchanged on the way out
}