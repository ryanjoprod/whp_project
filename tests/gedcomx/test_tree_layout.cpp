#include <doctest/doctest.h>

#include "gedcomx/tree_layout.h"

using gedcomx::TreeLayoutInput;
using gedcomx::assignGenerations;

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