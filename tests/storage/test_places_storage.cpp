#include <doctest/doctest.h>

#include <stdexcept>

#include "test_helpers.h"
#include "storage/database.h"
#include "gedcomx/place_description.h"
#include "gedcomx/citation.h"

namespace {
	gedcomx::PlaceDescription makePlaceWithParent(const std::string& id, const std::string& parentId) {
		gedcomx::PlaceDescription p;
		p.id = id;
		if (!parentId.empty())
			p.jurisdictions.push_back({ parentId,{} });
		return p;
	}
}

TEST_CASE("Top-level places save with no jurisdiction") {
	storage::Database db(":memory:");

	gedcomx::PlaceDescription country;
	country.id = "PL1";
	CHECK_NOTHROW(db.save(country));

	auto places = db.loadAllPlaces();
	REQUIRE(places.size() == 1);
	CHECK(places[0].jurisdictions.empty());
}

TEST_CASE("A place can be save before its parent inside a transaction") {
	storage::Database db(":memory:");

	gedcomx::PlaceDescription city;
	city.id = "PL2";
	city.jurisdictions.push_back({ "PL1" });
	gedcomx::PlaceDescription country;
	country.id = "PL1";

	CHECK_NOTHROW(db.runInTransaction([&] {
		db.save(city);
		db.save(country);
	}));
	CHECK(db.loadAllPlaces().size() == 2);
}

TEST_CASE("A place whose jurisdiction does not exist is rejected") {
	storage::Database db(":memory:");

	gedcomx::PlaceDescription city;
	city.id = "PL2";
	city.jurisdictions.push_back({ "NOPE" });

	CHECK_THROWS_AS(db.save(city), std::runtime_error);
	CHECK_THROWS_AS(db.runInTransaction([&] {
		db.save(city);
	}), std::runtime_error);
	CHECK(db.loadAllPlaces().empty());  // nothing was left behind
}

TEST_CASE("A place round-trips with its code, dated names, URLs, notes and citations") {
	storage::Database db(":memory:");
	testutil::saveSourceWithCitation(db, "SD1");

	gedcomx::PlaceDescription p;
	p.id = "PL1";
	p.type = "http://gedcomx.org/City";
	p.code = "91000";
	p.names.push_back({ "en", "Jerusalem",{} });
	p.names.push_back({ "he", "Yerushalayim", testutil::makeHebrewDate() });
	p.urls.push_back({ "https://example.org/jerusalem", "Overview", "Web Home" });
	p.conclusion.notes.push_back({ "en", "", "A note." });
	p.conclusion.sources.push_back({ gedcomx::bareCitationId("SD1") });
	db.save(p);

	auto places = db.loadAllPlaces();
	REQUIRE(places.size() == 1);
	CHECK(places[0].code == "91000");

	REQUIRE(places[0].names.size() == 2);
	CHECK(places[0].primaryName() == "Jerusalem");
	CHECK(places[0].names[0].date.calendar == "");
	CHECK(places[0].names[1].value == "Yerushalayim");
	testutil::checkHebrewDate(places[0].names[1].date);

	REQUIRE(places[0].urls.size() == 1);
	CHECK(places[0].urls[0].url == "https://example.org/jerusalem");
	CHECK(places[0].urls[0].description == "Overview");
	CHECK(places[0].urls[0].type == "Web Home");

	REQUIRE(places[0].conclusion.notes.size() == 1);
	REQUIRE(places[0].conclusion.sources.size() == 1);
	CHECK(places[0].conclusion.sources[0].citationId == gedcomx::bareCitationId("SD1"));
}

TEST_CASE("A place can belong to different parents over time, in order") {
	storage::Database db(":memory:");

	gedcomx::PlaceDescription a;
	a.id = "PL_A";
	gedcomx::PlaceDescription b;
	b.id = "PL_B";
	gedcomx::PlaceDescription town;
	town.id = "PL_T";
	town.jurisdictions.push_back({ "PL_A",{} });
	town.jurisdictions.push_back({ "PL_B", testutil::makeHebrewDate() });

	db.runInTransaction([&] {
		db.save(town);
		db.save(a);
		db.save(b);
	});

	for (const auto& place : db.loadAllPlaces()) {
		if (place.id != "PL_T") continue;
		REQUIRE(place.jurisdictions.size() == 2);
		CHECK(place.jurisdictions[0].parentId == "PL_A");
		CHECK(place.jurisdictions[1].parentId == "PL_B");
		testutil::checkHebrewDate(place.jurisdictions[1].date);
	}
}

TEST_CASE("Re-saving a place replaces its names, parents and URLs") {
	storage::Database db(":memory:");

	gedcomx::PlaceDescription parent;
	parent.id = "PL_P";
	db.save(parent);

	gedcomx::PlaceDescription p;
	p.id = "PL1";
	p.names.push_back({ "en", "Old",{} });
	p.names.push_back({ "en", "Older",{} });
	p.jurisdictions.push_back({ "PL_P",{} });
	p.urls.push_back({ "https://old.example", "", "" });
	db.save(p);

	p.names.pop_back();
	p.jurisdictions.clear();
	p.urls.clear();
	db.save(p);

	for (const auto& place : db.loadAllPlaces()) {
		if (place.id != "PL1") continue;
		CHECK(place.names.size() == 1);
		CHECK(place.jurisdictions.empty());
		CHECK(place.urls.empty());
	}
}

TEST_CASE("A place cannot be its own parent") {
	storage::Database db(":memory:");
	CHECK_THROWS(db.save(makePlaceWithParent("PL1", "PL1")));
}

TEST_CASE("A place cannot become an ancestor of its own ancestor") {
	storage::Database db(":memory:");
	db.save(makePlaceWithParent("PL1", ""));
	db.save(makePlaceWithParent("PL2", "PL1"));
	db.save(makePlaceWithParent("PL3", "PL2"));

	// PL1 under PL3 would close the loo PL1 -> PL3 -> PL2 -> PL1
	CHECK_THROWS(db.save(makePlaceWithParent("PL1", "PL3")));
}

TEST_CASE("A refused place leaves the old hierarchy intact") {
	storage::Database db(":memory:");
	db.save(makePlaceWithParent("PL1", ""));
	db.save(makePlaceWithParent("PL2", "PL1"));

	CHECK_THROWS(db.save(makePlaceWithParent("PL1", "PL2")));

	auto places = db.loadAllPlaces();
	for (const auto& p : places) {
		if (p.id == "PL1")
			CHECK(p.jurisdictions.empty());
	}
}

TEST_CASE("A cycle built across two saves in one transaction is refused") {
	storage::Database db(":memory:");

	CHECK_THROWS(db.runInTransaction([&] {
		db.save(makePlaceWithParent("PL1", "PL2"));  // PL2 does not exist yet; allowed until commit
		db.save(makePlaceWithParent("PL2", "PL1"));  // this one closes the loop
	}));
	CHECK(db.loadAllPlaces().empty());  // the whole transaction rolled back
}

TEST_CASE("Two places that share a parent are not a cycle") {
	storage::Database db(":memory:");
	db.save(makePlaceWithParent("PL1", ""));
	db.save(makePlaceWithParent("PL2", "PL1"));
	CHECK_NOTHROW(db.save(makePlaceWithParent("PL3", "PL1")));
	CHECK_NOTHROW(db.save(makePlaceWithParent("PL4", "PL2")));  // a deeper, normal chain
}