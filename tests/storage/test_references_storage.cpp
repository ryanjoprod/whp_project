#include <doctest/doctest.h>
#include <set>

#include "storage/database.h"
#include "test_helpers.h"

namespace {
	gedcomx::Person makePerson(const std::string& id, const std::string& fullName) {
		gedcomx::Person p;
		p.id = id;
		gedcomx::Name name;
		gedcomx::NameForm form;
		form.fullText = fullName;
		name.nameForms.push_back(form);
		p.names.push_back(name);

		return p;
	}

	gedcomx::Citation makeCitation(const std::string& id, const std::string& sourceId, const std::string& page) {
		gedcomx::Citation c;
		c.id = id;
		c.sourceId = sourceId;
		c.page = page;

		return c;
	}
}

TEST_CASE("loadCitationReferences finds everything that cites a citation") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	db.save(makeCitation("C1", "S1", "p. 1"));
	db.save(makeCitation("C2", "S1", "p. 2"));

	gedcomx::Person abraham = makePerson("P1", "Abraham");
	abraham.conclusion.sources.push_back({ "C1" });
	gedcomx::Fact birth;
	birth.id = "F1";
	birth.type = "http://gedcomx.org/Birth";
	birth.conclusion.sources.push_back({ "C1" });
	abraham.facts.push_back(birth);
	db.save(abraham, "S1");

	gedcomx::Person sarah = makePerson("P2", "Sarah");
	sarah.conclusion.sources.push_back({ "C2" });  // a different citation: must not appear for C1
	db.save(sarah, "S1");

	gedcomx::Relationship rel;
	rel.id = "R1";
	rel.type = "http://gedcomx.org/Couple";
	rel.person1Id = "P1";
	rel.person2Id = "P2";
	rel.conclusion.sources.push_back({ "C1" });
	gedcomx::Fact marriage;
	marriage.id = "F1";
	marriage.type = "http://gedcomx.org/Marriage";
	marriage.conclusion.sources.push_back({ "C1" });
	rel.facts.push_back(marriage);
	db.save(rel, "S1");

	gedcomx::Event e;
	e.id = "E1";
	e.type = "http://gedcomx.org/Baptism";
	e.description = "Naming";
	e.conclusion.sources.push_back({ "C1" });
	gedcomx::EventRole role;
	role.personId = "P2";
	role.type = "http://gedcomx.org/Witness";
	role.conclusion.sources.push_back({ "C1" });
	e.roles.push_back(role);
	db.save(e, "S1");

	gedcomx::PlaceDescription place;
	place.id = "PL1";
	place.names.push_back({ "en", "Jerusalem",{} });
	place.conclusion.sources.push_back({ "C1" });
	db.save(place);

	auto refs = db.loadCitationReferences("C1");
	CHECK(refs.size() == 7);

	std::set<std::string> kinds;
	for (const auto& r : refs) kinds.insert(r.kind);
	const std::set<std::string> expected = { "Person", "Person fact", "Relationship", "Relationship fact", "Event", "Event role", "Place" };
	CHECK(kinds == expected);

	for (const auto& r : refs) {
		CHECK(r.citationId == "C1");
		if (r.kind == "Person") CHECK(r.label == "Abraham");
		if (r.kind == "Person fact") CHECK(r.label == "Birth of Abraham");
		if (r.kind == "Relationship") CHECK(r.label == "Couple: Abraham & Sarah");
		if (r.kind == "Relationship fact") CHECK(r.label == "Marriage: Abraham & Sarah");
		if (r.kind == "Event") CHECK(r.label == "Baptism - Naming");
		if (r.kind == "Event role") CHECK(r.label == "Witness Sarah in Baptism");
		if (r.kind == "Place") CHECK(r.label == "Jerusalem");
	}

	CHECK(db.loadCitationReferences("C2").size() == 1);
	CHECK(db.loadCitationReferences("NOPE").empty());
}

TEST_CASE("loadSourceReferences covers every citation of the source, including whole-source ones") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	const std::string whole = db.ensureBareCitation("S1");
	db.save(makeCitation("C1", "S1", "p. 1"));
	db.save(makeCitation("C_OTHER", "S2", "p. 9"));

	gedcomx::Person a = makePerson("P1", "A");
	a.conclusion.sources.push_back({ whole });
	gedcomx::Person b = makePerson("P2", "B");
	b.conclusion.sources.push_back({ "C1" });
	gedcomx::Person c = makePerson("P3", "C");
	c.conclusion.sources.push_back({ "C_OTHER" });  // cites S2, so it's not a reference to S1
	db.save(a, "S1");
	db.save(b, "S1");
	db.save(c, "S1");

	auto refs = db.loadSourceReferences("S1");
	REQUIRE(refs.size() == 2);
	std::set<std::string> citationIds;
	for (const auto& r : refs) citationIds.insert(r.citationId);
	const std::set<std::string> expected = { whole, "C1" };
	CHECK(citationIds == expected);
}