#include <doctest/doctest.h>

#include "gedcomx/parser.h"

TEST_CASE("SourceDescription round-trips through serialize/parse") {
    auto original = nlohmann::json::parse(
        "{\"id\":\"S1\",\"titles\":[{\"value\":\"1900 Census\"}],"
        "\"citations\":[{\"value\":\"US Census Bureau, 1900\"}]}");

    gedcomx::SourceDescription source = gedcomx::parseSourceDescription(original);
    nlohmann::json serialized = gedcomx::serializeSourceDescription(source);
    gedcomx::SourceDescription reparsed = gedcomx::parseSourceDescription(serialized);

    CHECK(reparsed.id == source.id);
    CHECK(reparsed.primaryTitle() == source.primaryTitle());
}