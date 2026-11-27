#include <doctest/doctest.h>

#include <cmath>

#include "gedcomx/tree_layout.h"

using gedcomx::TreeLayoutInput;
using gedcomx::assignGenerations;
using gedcomx::layoutTree;

TEST_CASE("assignGenerations puts a lone person on row 0") {
	TreeLayoutInput in;
	in.people = { "A" };
	in.rootId = "A";

	auto rows = assignGenerations(in);
	CHECK(rows.at("A") == 0);
}

TEST_CASE("assignGenerations puts parents above and children below the root") {
	TreeLayoutInput in;
	in.people = { "F", "M", "C" };
	in.couples = { { "F", "M" } };
	in.parentChild = { { "F", "C" },{ "M", "C" } };

	in.rootId = "C";
	auto fromChild = assignGenerations(in);
	CHECK(fromChild.at("F") == -1);
	CHECK(fromChild.at("M") == -1);
	CHECK(fromChild.at("C") == 0);

	in.rootId = "F";
	auto fromParent = assignGenerations(in);
	CHECK(fromParent.at("F") == 0);
	CHECK(fromParent.at("M") == 0);
	CHECK(fromParent.at("C") == 1);
}

TEST_CASE("assignGenerations puts siblings on the same row") {
	TreeLayoutInput in;
	in.people = { "P1", "P2", "K1", "K2" };
	in.couples = { { "P1", "P2" } };
	in.parentChild = { { "P1", "K1" },{ "P2", "K1" },{ "P1", "K2" },{ "P2", "K2" } };
	in.rootId = "K1";

	auto rows = assignGenerations(in);
	CHECK(rows.at("P1") == -1);
	CHECK(rows.at("K1") == 0);
	CHECK(rows.at("K2") == 0);
}

TEST_CASE("assignGenerations lines up both side of a marriage") {
	// The root's father has two more generations of known ancestors, the mother's parents are known too.
	// The mother's parents must sit on the SAME row as the father's parents, directly above their children.
	TreeLayoutInput in;
	in.people = { "GG1", "GG2", "FP1", "FP2", "MP1", "MP2", "F", "M", "C" };
	in.couples = { { "GG1", "GG2" },{ "FP1", "FP2" },{ "MP1", "MP2" },{ "F", "M" } };
	in.parentChild = { { "GG1", "FP1" },{ "FP1", "F" },{ "MP1", "M" },{ "F", "C" },{ "M", "C" } };
	in.rootId = "C";

	auto rows = assignGenerations(in);
	CHECK(rows.at("GG1") == -3);
	CHECK(rows.at("FP1") == -2);
	CHECK(rows.at("MP1") == -2);
	CHECK(rows.at("MP2") == -2);
	CHECK(rows.at("F") == -1);
	CHECK(rows.at("M") == -1);
	CHECK(rows.at("C") == 0);
}

TEST_CASE("assignGenerations keeps several partners of one person on one row") {
	TreeLayoutInput in;
	in.people = { "A", "B", "C", "D" };
	in.couples = { { "A", "B" },{ "A", "C" } };
	in.parentChild = { { "A", "D" },{ "B", "D" } };
	in.rootId = "A";

	auto rows = assignGenerations(in);
	CHECK(rows.at("A") == 0);
	CHECK(rows.at("B") == 0);
	CHECK(rows.at("C") == 0);
	CHECK(rows.at("D") == 1);
}

TEST_CASE("assignGenerations ignores ids it was not given") {
	TreeLayoutInput in;
	in.people = { "A" };
	in.couples = { { "A", "NOBODY" } };
	in.parentChild = { { "NOBODY", "A" } };
	in.rootId = "A";

	auto rows = assignGenerations(in);
	CHECK(rows.size() == 1);
	CHECK(rows.at("A") == 0);
}

TEST_CASE("assignGenerations survives a parent-child cycle in the data") {
	TreeLayoutInput in;
	in.people = { "A", "B" };
	in.parentChild = { { "A", "B" },{ "B", "A" } };
	in.rootId = "A";

	auto rows = assignGenerations(in);  // the point: it returns at all
	CHECK(rows.size() == 2);
}

TEST_CASE("assignGenerations normalizes a disconnected group on its own") {
	TreeLayoutInput in;
	in.people = { "X", "Y", "Z" };
	in.parentChild = { { "Y", "Z" } };
	in.rootId = "X";

	auto rows = assignGenerations(in);
	CHECK(rows.at("X") == 0);
	CHECK(rows.at("Y") == 0);
	CHECK(rows.at("Z") == 1);
}

TEST_CASE("layoutTree puts a lone person at the origin") {
	TreeLayoutInput in;
	in.people = { "A" };
	in.rootId = "A";

	auto at = layoutTree(in);
	CHECK(at.at("A").x == doctest::Approx(0.0));
	CHECK(at.at("A").y == doctest::Approx(0.0));
}

TEST_CASE("layoutTree places partners side by side, one person-width apart") {
	TreeLayoutInput in;
	in.people = { "A", "B" };
	in.couples = { { "A", "B" } };
	in.rootId = "A";

	auto at = layoutTree(in);
	CHECK(at.at("A").y == doctest::Approx(at.at("B").y));
	CHECK(at.at("B").x - at.at("A").x == doctest::Approx(1.0));
}

TEST_CASE("layoutTree keeps a chain of partners in one line") {
	TreeLayoutInput in;
	in.people = { "A", "B", "C" };
	in.couples = { { "A", "B" },{ "A", "C" } };
	in.rootId = "A";

	auto at = layoutTree(in);  // A is in the middle: B, A, C
	CHECK(at.at("A").x - at.at("B").x == doctest::Approx(1.0));
	CHECK(at.at("C").x - at.at("A").x == doctest::Approx(1.0));
}

TEST_CASE("layoutTree centers a child under a couple") {
	TreeLayoutInput in;
	in.people = { "F", "M", "C" };
	in.couples = { { "F", "M" } };
	in.parentChild = { { "F", "C" },{ "M", "C" } };
	in.rootId = "C";

	auto at = layoutTree(in);
	CHECK(at.at("C").x == doctest::Approx(0.0));
	CHECK(at.at("F").y == doctest::Approx(-1.0));
	CHECK(at.at("C").y == doctest::Approx(0.0));
	CHECK((at.at("F").x + at.at("M").x) / 2.0 == doctest::Approx(at.at("C").x));
}

TEST_CASE("layoutTree centers parents over their children and keeps siblings apart") {
	TreeLayoutInput in;
	in.people = { "P1", "P2", "K1", "K2" };
	in.couples = { { "P1", "P2" } };
	in.parentChild = { { "P1", "K1" },{ "P2", "K1" },{ "P1", "K2" },{ "P2", "K2" } };
	in.rootId = "K1";

	auto at = layoutTree(in);
	CHECK(at.at("K1").x == doctest::Approx(0.0));
	CHECK(at.at("K2").x - at.at("K1").x == doctest::Approx(1.5));  // a person-width plus the gap
	CHECK((at.at("P1").x + at.at("P2").x) / 2.0 == doctest::Approx((at.at("K1").x + at.at("K2").x) / 2.0));
}

TEST_CASE("layoutTree never overlaps two people on the same row") {
	TreeLayoutInput in;
	in.people = { "GG1", "GG2", "FP1", "FP2", "MP1", "MP2", "F", "M", "C" };
	in.couples = { { "GG1", "GG2" },{ "FP1", "FP2" },{ "MP1", "MP2" },{ "F", "M" } };
	in.parentChild = { { "GG1", "FP1" },{ "FP1", "F" },{ "MP1", "M" },{ "F", "C" },{ "M", "C" } };
	in.rootId = "C";

	auto at = layoutTree(in);
	CHECK(at.at("C").x == doctest::Approx(0.0));

	for (const auto& a : at) {
		for (const auto& b : at) {
			if (a.first >= b.first) continue;  // each pair once
			if (a.second.y != b.second.y) continue;
			CHECK(std::abs(a.second.x - b.second.x) >= 0.999);
		}
	}
}

TEST_CASE("layoutTree gives the same answer every time") {
	TreeLayoutInput in;
	in.people = { "P1", "P2", "K1", "K2", "K3" };
	in.couples = { { "P1", "P2" } };
	in.parentChild = { { "P1", "K1" },{ "P2", "K1" },{ "P1", "K2" },{ "P2", "K2" },{ "P1", "K3" },{ "P2", "K3" } };
	in.rootId = "K2";

	auto first = layoutTree(in);
	auto second = layoutTree(in);
	for (const auto& entry : first) {
		CHECK(entry.second.x == doctest::Approx(second.at(entry.first).x));
		CHECK(entry.second.y == doctest::Approx(second.at(entry.first).y));
	}
}