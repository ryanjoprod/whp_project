#include <doctest/doctest.h>

#include "gedcomx/date_parts.h"
#include "gedcomx/place_logic.h"

namespace {
	gedcomx::PlaceDescription makePlace(const std::string& id, const std::string& name, const std::string parent = "") {
		gedcomx::PlaceDescription p;
		p.id = id;
		if (!name.empty()) p.names.push_back({ "", name,{} });
		if (!parent.empty()) p.jurisdictions.push_back({ parent,{} });
		return p;
	}

	std::vector<gedcomx::PlaceDescription> virginia() {
		return { makePlace("VA", "Virginia"), makePlace("FC", "Fairfax County", "VA"), makePlace("FX", "Fairfax", "FC") };
	}
}

TEST_CASE("placeTitle walks up the first parent chain") {
	const auto places = virginia();
	const auto index = gedcomx::indexPlaces(places);
	CHECK_EQ(gedcomx::placeTitle(places[2], index), "Fairfax, Fairfax County, Virginia");
	CHECK_EQ(gedcomx::placeTitle(places[0], index), "Virginia");
}

TEST_CASE("placeTitle stops at a missing parent and on a loop") {
	std::vector<gedcomx::PlaceDescription> places = { makePlace("X", "Orphan", "GONE") };
	CHECK_EQ(gedcomx::placeTitle(places[0], gedcomx::indexPlaces(places)), "Orphan");

	places = { makePlace("A", "A", "B"), makePlace("B", "B", "A") };
	CHECK_EQ(gedcomx::placeTitle(places[0], gedcomx::indexPlaces(places)), "A, B");
}

TEST_CASE("placeTitle handles unnamed and unsaved places") {
	const auto places = virginia();
	const auto index = gedcomx::indexPlaces(places);
	CHECK_EQ(gedcomx::placeTitle(makePlace("N", ""), index), "(unnamed place)");
	CHECK_EQ(gedcomx::placeTitle(makePlace("", "Reston", "FC"), index), "Reston, Fairfax County, Virginia");
}

TEST_CASE("placeDescendants lists everything below, never the place itself") {
	const auto places = virginia();
	const std::unordered_set<std::string> below = { "FC", "FX" };
	CHECK(gedcomx::placeDescendants("VA", places) == below);
	CHECK(gedcomx::placeDescendants("FX", places).empty());
}

TEST_CASE("placeDescendants ends on a loop in bad data") {
	const std::vector<gedcomx::PlaceDescription> places = { makePlace("A", "A", "B"), makePlace("B", "B", "A") };
	const std::unordered_set<std::string> expected = { "B" };
	CHECK(gedcomx::placeDescendants("A", places) == expected);
}

TEST_CASE("parseCoordinate") {
	std::optional<double> v;
	CHECK(gedcomx::parseCoordinate("", 90, v).empty());
	CHECK(!v.has_value());

	CHECK(gedcomx::parseCoordinate(" 38.8462 ", 90, v).empty());
	REQUIRE(v.has_value());
	CHECK(*v == doctest::Approx(38.8462));

	CHECK(!gedcomx::parseCoordinate("abc", 90, v).empty());
	CHECK(!v.has_value());
	CHECK(!gedcomx::parseCoordinate("12.5x", 90, v).empty());
	CHECK(!gedcomx::parseCoordinate("nan", 90, v).empty());
	CHECK(!gedcomx::parseCoordinate("91", 90, v).empty());
	CHECK(!gedcomx::parseCoordinate("-181", 180, v).empty());
	CHECK(gedcomx::parseCoordinate("-180", 180, v).empty());
}

TEST_CASE("formatCoordinate trims trailing zeros") {
	CHECK_EQ(gedcomx::formatCoordinate(38.8462), "38.8462");
	CHECK_EQ(gedcomx::formatCoordinate(-77.3), "-77.3");
	CHECK_EQ(gedcomx::formatCoordinate(5.0), "5");
	CHECK_EQ(gedcomx::formatCoordinate(-0.0), "0");
}

TEST_CASE("validatePlace") {
	CHECK(gedcomx::validatePlace(makePlace("A", "Fairfax", "B")).empty());
	CHECK(!gedcomx::validatePlace(makePlace("A", "")).empty());
	CHECK(!gedcomx::validatePlace(makePlace("A", "   ")).empty());
	CHECK(!gedcomx::validatePlace(makePlace("A", "Fairfax", "A")).empty());

	auto blankUrl = makePlace("A", "Fairfax");
	blankUrl.urls.push_back({ "  ", "", "" });
	CHECK(!gedcomx::validatePlace(blankUrl).empty());

	auto noParent = makePlace("A", "Fairfax");
	noParent.jurisdictions.push_back({ "",{} });
	CHECK(!gedcomx::validatePlace(noParent).empty());
}