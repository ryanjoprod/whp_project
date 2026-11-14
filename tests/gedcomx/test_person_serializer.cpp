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

TEST_CASE("Every Person field survives serialize and parse") {
    gedcomx::Person p;
    p.id = "P1";
    p.gender = gedcomx::Gender{ "http://gedcomx.org/Female" };

    gedcomx::Name name;
    name.title = "Dr.";
    name.nickName = "Annie";
    name.callName = "Ann";

    gedcomx::NameForm form;
    form.fullText = "Anne Smith";
    form.parts.push_back({ "Anne", "https://gedcomx.org/Given" });
    form.parts.push_back({ "Smith", "https://gedcomx.org/Surname" });
    name.nameForms.push_back(form);
    p.names.push_back(name);

    p.attributes.push_back({ "A1", "Occupation", "Baker" });

    gedcomx::Address addr;
    addr.id = "AD1";
    addr.street = "1 Main St";
    addr.city = "Springfield";
    addr.stateOrProvince = "IL";
    addr.postalCode = "62701";
    addr.country = "USA";
    p.addresses.push_back(addr);

    gedcomx::Person back = gedcomx::parsePerson(gedcomx::serializePerson(p));

    REQUIRE(back.names.size() == 1);
    CHECK(back.names[0].title == "Dr.");
    CHECK(back.names[0].nickName == "Annie");
    CHECK(back.names[0].callName == "Ann");

    REQUIRE(back.attributes.size() == 1);
    CHECK(back.attributes[0].id == "A1");
    CHECK(back.attributes[0].type == "Occupation");
    CHECK(back.attributes[0].value == "Baker");

    REQUIRE(back.addresses.size() == 1);
    CHECK(back.addresses[0].id == "AD1");
    CHECK(back.addresses[0].street == "1 Main St");
    CHECK(back.addresses[0].city == "Springfield");
    CHECK(back.addresses[0].stateOrProvince == "IL");
    CHECK(back.addresses[0].postalCode == "62701");
    CHECK(back.addresses[0].country == "USA");
}