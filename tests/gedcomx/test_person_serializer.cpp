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