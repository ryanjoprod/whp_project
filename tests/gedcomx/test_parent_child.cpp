#include <doctest/doctest.h>

#include <functional>
#include <memory>

#include "gedcomx/parent_child.h"

namespace {
	std::function<std::string()> counter() {
		auto n = std::make_shared<int>(0);
		return [n] { return "R_new" + std::to_string(++*n); };
	}

	// An existing link as the database returns it: row-key person ids, local link id.
	gedcomx::Relationship existingLink(const std::string& id, const std::string& parentage) {
		gedcomx::Relationship r;
		r.id = id;
		r.sourceId = "S1";
		r.type = gedcomx::kParentChildType;
		r.person1Id = "S1:P1";
		r.person2Id = "S1:P3";

		if (!parentage.empty()) {
			gedcomx::Fact f;
			f.type = parentage;
			r.facts.push_back(f);
		}

		return r;
	}
	
	int countType(const gedcomx::Relationship& r, const std::string& type) {
		int n = 0;
		for (const auto& f : r.facts)
			if (f.type == type) ++n;

		return n;
	}
}

TEST_CASE("A new child gets one link per parent, typed individually") {
	gedcomx::ChildLinkInput in;
	in.childId = "P3";
	in.parent[0] = { true, gedcomx::kBiologicalParent, std::nullopt };
	in.parent[1] = { true, "", std::nullopt };  // unspecified

	auto plan = gedcomx::planChildLinks("P1", "P2", { in }, {}, counter());

	REQUIRE(plan.save.size() == 2);
	CHECK(plan.deleteIds.empty());

	CHECK(plan.save[0].id == "R_new1");
	CHECK(plan.save[0].type == gedcomx::kParentChildType);
	CHECK(plan.save[0].person1Id == "P1");  // parent
	CHECK(plan.save[0].person2Id == "P3");  // child
	REQUIRE(plan.save[0].facts.size() == 1);
	CHECK(plan.save[0].facts[0].type == gedcomx::kBiologicalParent);

	CHECK(plan.save[1].person1Id == "P2");
	CHECK(plan.save[1].facts.empty());  // unspecified stores no fact
}

TEST_CASE("CHanging parentage keeps the link id and its other facts") {
	gedcomx::Relationship link = existingLink("R_old", gedcomx::kBiologicalParent);
	gedcomx::Fact other;
	other.type = "http://example.com/CustomFact";
	link.facts.push_back(other);

	gedcomx::ChildLinkInput in;
	in.childId = "P3";
	in.parent[0] = { true, gedcomx::kAdoptiveParent, link };
	in.parent[1] = { false, "", std::nullopt };

	auto plan = gedcomx::planChildLinks("P1", "P2", { in }, {}, counter());

	REQUIRE(plan.save.size() == 1);
	const auto& out = plan.save[0];
	CHECK(out.id == "R_old");  // not a new id
	CHECK(out.person1Id == "P1");  // row key converted back to local
	CHECK(out.person2Id == "P3");
	CHECK(countType(out, gedcomx::kAdoptiveParent) == 1);
	CHECK(countType(out, gedcomx::kBiologicalParent) == 0);  // old parentage replaced, not added to
	CHECK(countType(out, "http://example.com/CustomFact") == 1);  // unrelated face survives
}

TEST_CASE("Setting an existing link to Not a parent deleted it") {
	gedcomx::ChildLinkInput in;
	in.childId = "P3";
	in.parent[0] = { false, "", existingLink("R_old", gedcomx::kBiologicalParent) };
	in.parent[1] = { true, "", std::nullopt };
	
	auto plan = gedcomx::planChildLinks("P1", "P2", { in }, {}, counter());

	REQUIRE(plan.deleteIds.size() == 1);
	CHECK(plan.deleteIds[0] == "R_old");
	REQUIRE(plan.save.size() == 1);
	CHECK(plan.save[0].person1Id == "P2");  // only the other parent's link remains
}

TEST_CASE("Not a parent with no existing link does nothing") {
	gedcomx::ChildLinkInput in;
	in.childId = "P3";
	in.parent[0] = { false, "", std::nullopt };
	in.parent[1] = { false, "", std::nullopt };

	auto plan = gedcomx::planChildLinks("P1", "P2", { in }, {}, counter());
	CHECK(plan.save.empty());
	CHECK(plan.deleteIds.empty());
}

TEST_CASE("Removed links are deleted") {
	auto plan = gedcomx::planChildLinks("P1", "P2", {}, { existingLink("R_gone", "") }, counter());
	CHECK(plan.save.empty());
	REQUIRE(plan.deleteIds.size() == 1);
	CHECK(plan.deleteIds[0] == "R_gone");
}

TEST_CASE("A child can never be one of the parents") {
	gedcomx::ChildLinkInput in;
	in.childId = "P1";
	in.parent[0] = { true, "", std::nullopt };
	in.parent[1] = { true, "", std::nullopt };

	auto plan = gedcomx::planChildLinks("P1", "P2", { in }, {}, counter());
	CHECK(plan.save.empty());
}

TEST_CASE("With one parent, only that parent gets links") {
	gedcomx::ChildLinkInput in;
	in.childId = "P3";
	in.parent[0] = { true, gedcomx::kBiologicalParent, std::nullopt };
	in.parent[1] = { true, "", std::nullopt };  // must be ignored: no second parent exists

	auto plan = gedcomx::planChildLinks("P1", "", { in }, {}, counter());

	REQUIRE(plan.save.size() == 1);
	CHECK(plan.save[0].person1Id == "P1");
	CHECK(plan.save[0].person2Id == "P3");
}

TEST_CASE("parentageTypeOf reads the parentage fact") {
	CHECK(gedcomx::parentageTypeOf(existingLink("R1", gedcomx::kStepParent)) == gedcomx::kStepParent);
	CHECK(gedcomx::parentageTypeOf(existingLink("R2", "")).empty());
}