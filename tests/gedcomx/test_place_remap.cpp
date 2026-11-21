#include <doctest/doctest.h>

#include "gedcomx/parser.h"

namespace {
	gedcomx::PlaceDescription makePlace(const std::string& id) {
		gedcomx::PlaceDescription p;
		p.id = id;
		return p;
	}

	gedcomx::Person personAt(const std::string& placeId) {
		gedcomx::Person person;
		person.id = "P1";
		gedcomx::Fact f;
		f.type = "http://gedcomx.org/Birth";
		f.place.original = "Somewhere";
		f.place.descriptionId = placeId;
		person.facts.push_back(f);
		return person;
	}
}

TEST_CASE("Remapping gives places and agents source-scoped ids") {
	std::vector<gedcomx::PlaceDescription> places = { makePlace("PL1") };
	std::vector<gedcomx::Agent> agents(1);
	agents[0].id = "A1";
	std::vector<gedcomx::Person> people;
	std::vector<gedcomx::Relationship> rels;
	std::vector<gedcomx::Event> events;

	gedcomx::remapPlaceAndAgentIds("S1", places, agents, people, rels, events);

	CHECK(places[0].id == "S1:PL1");
	CHECK(agents[0].id == "S1:A1");
}

TEST_CASE("Remapping updates parents and the palce on facts and events") {
	std::vector<gedcomx::PlaceDescription> places = { makePlace("PL1"), makePlace("PL2") };
	places[1].jurisdictions.push_back({ "PL1" });
	std::vector<gedcomx::Agent> agents;
	std::vector<gedcomx::Person> people = { personAt("PL2") };
	std::vector<gedcomx::Relationship> rels(1);
	gedcomx::Fact relFact;
	relFact.place.descriptionId = "PL1";
	rels[0].facts.push_back(relFact);
	std::vector<gedcomx::Event> events(1);
	events[0].place.descriptionId = "PL2";

	gedcomx::remapPlaceAndAgentIds("S1", places, agents, people, rels, events);

	REQUIRE(places[1].jurisdictions.size() == 1);
	CHECK(places[1].jurisdictions[0].parentId == "S1:PL1");
	CHECK(people[0].facts[0].place.descriptionId == "S1:PL2");
	CHECK(rels[0].facts[0].place.descriptionId == "S1:PL1");
	CHECK(events[0].place.descriptionId == "S1:PL2");
}

TEST_CASE("A reference to a place the file does not define is cleared but keeps its text") {
	std::vector<gedcomx::PlaceDescription> places = { makePlace("PL1") };
	places[0].jurisdictions.push_back({ "PL_MISSING" });
	std::vector<gedcomx::Agent> agents;
	std::vector<gedcomx::Person> people = { personAt("PL_MISSING") };
	std::vector<gedcomx::Relationship> rels;
	std::vector<gedcomx::Event> events;

	gedcomx::remapPlaceAndAgentIds("S1", places, agents, people, rels, events);

	CHECK(places[0].jurisdictions.empty());
	CHECK(people[0].facts[0].place.descriptionId.empty());
	CHECK(people[0].facts[0].place.original == "Somewhere");
}

TEST_CASE("A fact with no place id is let alone") {
	std::vector<gedcomx::PlaceDescription> places = { makePlace("PL1") };
	std::vector<gedcomx::Agent> agents;
	std::vector<gedcomx::Person> people = { personAt("") };
	std::vector<gedcomx::Relationship> rels;
	std::vector<gedcomx::Event> events;

	gedcomx::remapPlaceAndAgentIds("S1", places, agents, people, rels, events);

	CHECK(people[0].facts[0].place.descriptionId.empty());
	CHECK(people[0].facts[0].place.original == "Somewhere");
}