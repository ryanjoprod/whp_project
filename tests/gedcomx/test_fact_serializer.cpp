#include <doctest/doctest.h>

#include "gedcomx/parser.h"

TEST_CASE("Fact round-trips through serliaze/parse") {
    auto original = nlohmann::json::parse(
        "{\"id\":\"F1\",\"type\":\"http://gedcomx.org/Birth\","
        "\"date\":{\"original\":\"22 February 1732\",\"formal\":\"+1732-02-22\"},"
        "\"place\":{\"original\":\"Westmoreland County, Virginia\",\"description\":{\"resource\":\"#PL1\"}},"
        "\"confidence\":\"http://gedcomx.org/High\","
        "\"notes\":[{\"text\":\"Verified against family bible.\"}]}");

    gedcomx::Fact fact = gedcomx::parseFact(original);
    nlohmann::json serialized = gedcomx::serializeFact(fact);
    gedcomx::Fact reparsed = gedcomx::parseFact(serialized);

    CHECK(reparsed.id == fact.id);
    CHECK(reparsed.type == fact.type);
    CHECK(reparsed.date.original == fact.date.original);
    CHECK(reparsed.date.formal == fact.date.formal);
    CHECK(reparsed.place.original == fact.place.original);
    CHECK(reparsed.place.descriptionId == fact.place.descriptionId);
    CHECK(reparsed.conclusion.confidence == fact.conclusion.confidence);
    REQUIRE(reparsed.conclusion.notes.size() == fact.conclusion.notes.size());
    CHECK(reparsed.conclusion.notes[0].text == fact.conclusion.notes[0].text);
}

TEST_CASE("Fact with minimal fields serializes with empty placeholder keys") {
    gedcomx::Fact fact;
    fact.id = "F2";
    fact.type = "http://gedcomx.org/Death";
    // Everything else left default/empty

    nlohmann::json j = gedcomx::serializeFact(fact);

    CHECK(j.contains("id"));
    CHECK(j.contains("type"));
    CHECK_FALSE(j.contains("date"));
    CHECK_FALSE(j.contains("place"));
    CHECK_FALSE(j.contains("notes"));
    CHECK_FALSE(j.contains("confidence"));
}