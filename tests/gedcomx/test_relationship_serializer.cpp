#include <doctest/doctest.h>

#include "gedcomx/parser.h"

TEST_CASE("Relationship round-trips through serliaze/parse") {
    auto original = nlohmann::json::parse(
        "{\"id\":\"R1\",\"type\":\"http://gedcomx.org/Couple\","
        "\"person1\":{\"resource\":\"#P1\"},\"person2\":{\"resource\":\"#P2\"}}");

    gedcomx::Relationship rel = gedcomx::parseRelationship(original);
    nlohmann::json serialized = gedcomx::serializeRelationship(rel);
    gedcomx::Relationship reparsed = gedcomx::parseRelationship(serialized);

    CHECK(reparsed.id == rel.id);
    CHECK(reparsed.type == rel.type);
    CHECK(reparsed.person1Id == rel.person1Id);
    CHECK(reparsed.person2Id == rel.person2Id);
    CHECK(reparsed.person2Id == rel.person2Id);
}