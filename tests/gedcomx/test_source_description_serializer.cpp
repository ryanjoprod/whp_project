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

TEST_CASE("Source author, publication, abbreviation and notes survive serialize and parse") {
    gedcomx::SourceDescription s;
    s.id = "S1";
    s.titles.push_back({ "", "JPS" });
    s.author = "Various";
    s.publication = "The Jewish Publication Society 1999";
    s.abbreviation = "JPS";
    s.notes.push_back({ "en", "", "A note" });

    auto back = gedcomx::parseSourceDescription(gedcomx::serializeSourceDescription(s));

    CHECK(back.author == "Various");
    CHECK(back.publication == "The Jewish Publication Society 1999");
    CHECK(back.abbreviation == "JPS");
    REQUIRE(back.notes.size() == 1);
    CHECK(back.notes[0].text == "A note");
}

TEST_CASE("A source with no extra details writes none of the extra members") {
    gedcomx::SourceDescription s;
    s.id = "S1";
    s.titles.push_back({ "", "JPS" });

    auto json = gedcomx::serializeSourceDescription(s);

    CHECK_FALSE(json.contains("author"));
    CHECK_FALSE(json.contains("publication"));
    CHECK_FALSE(json.contains("abbreviation"));
    CHECK_FALSE(json.contains("notes"));
}