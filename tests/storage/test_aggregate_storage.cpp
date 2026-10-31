#include <doctest/doctest.h>

#include <algorithm>

#include "test_helpers.h"
#include "storage/database.h"

TEST_CASE("getAggregateIdForPerson resolves linked and unlinked persons correctly") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");

	gedcomx::Person p1;
	p1.id = "P1";
	db.save(p1, "S1");

	gedcomx::Person p2;
	p2.id = "P1";  // same local id, different source - a realistic merge case
	db.save(p2, "S2");

	gedcomx::Person p3;
	p3.id = "P2";  // stays unlinked
	db.save(p3, "S1");

	db.linkPersons("AGG1", { {"S1", "P1"}, {"S2", "P1"} });

	auto agg1 = db.getAggregateIdForPerson("S1:P1");
	REQUIRE(agg1.has_value());
	CHECK(*agg1 == "AGG1");

	auto agg2 = db.getAggregateIdForPerson("S2:P1");
	REQUIRE(agg2.has_value());
	CHECK(*agg2 == "AGG1");

	auto unlinked = db.getAggregateIdForPerson("S1:P2");
	CHECK_FALSE(unlinked.has_value());
}

TEST_CASE("Linking a third person to an existing pair joins the same aggregate") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	testutil::saveSource(db, "S3");

	gedcomx::Person p1;
	p1.id = "P1";
	db.save(p1, "S1");

	gedcomx::Person p2;
	p2.id = "P1";
	db.save(p2, "S2");


	gedcomx::Person p3;
	p3.id = "P1";
	db.save(p3, "S3");

	db.linkPersons("AGG1", { {"S1", "P1"}, {"S2", "P1"} });

	// Simulate what App::run()'s handler does: person3 links to person1,
	// who already belongs to AGG1 - person3 should join AGG1, not create
	// a new group.
	auto existing = db.getAggregateIdForPerson("S1:P1");
	REQUIRE(existing.has_value());
	db.linkPersons(*existing, { {"S3", "P1"} });

	CHECK(*db.getAggregateIdForPerson("S3:P1") == "AGG1");
	CHECK(*db.getAggregateIdForPerson("S2:P1") == "AGG1");
}

TEST_CASE("getLinkedPersonKeys finds aall counterparts, excluding self") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	testutil::saveSource(db, "S3");

	gedcomx::Person p1;
	p1.id = "P1";
	db.save(p1, "S1");

	gedcomx::Person p2;
	p2.id = "P1";
	db.save(p2, "S2");

	gedcomx::Person p3;
	p3.id = "P1";
	db.save(p3, "S3");

	db.linkPersons("AGG1", { {"S1", "P1"}, {"S2", "P1"}, {"S3", "P1"} });

	auto linked = db.getLinkedPersonKeys("S1:P1");
	REQUIRE(linked.size() == 2);
	CHECK(std::find(linked.begin(), linked.end(), "S2:P1") != linked.end());
	CHECK(std::find(linked.begin(), linked.end(), "S3:P1") != linked.end());
	CHECK(std::find(linked.begin(), linked.end(), "S1:P1") == linked.end());  // excludes self
}