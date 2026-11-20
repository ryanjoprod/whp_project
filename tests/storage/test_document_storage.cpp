#include <doctest/doctest.h>

#include <set>

#include "test_helpers.h"

#include "storage/database.h"
#include "gedcomx/document.h"

namespace {
	gedcomx::Document makeDocument(const std::string id, const std::string& text) {
		gedcomx::Document d;
		d.id = id;
		d.type = "http://gedcomx.org/Analysis";
		d.textType = "plain";
		d.extracted = true;
		d.text = text;
		return d;
	}
}

TEST_CASE("A document round-trips with its fields and notes, keyed by source") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Document d = makeDocument("D1", "Transcribed text");
	d.conclusion.confidence = "http://gedcomx.org/High";
	d.conclusion.lang = "en";
	d.conclusion.notes.push_back({ "en", "Subject", "A note" });
	db.save(d, "S1");

	auto docs = db.loadAllDocuments();
	REQUIRE(docs.size() == 1);

	const auto& loaded = docs[0];
	CHECK(loaded.id == "S1:D1");
	CHECK(loaded.sourceId == "S1");
	CHECK(loaded.type == "http://gedcomx.org/Analysis");
	CHECK(loaded.textType == "plain");
	CHECK(loaded.extracted);
	CHECK(loaded.text == "Transcribed text");
	CHECK(loaded.conclusion.confidence == "http://gedcomx.org/High");
	CHECK(loaded.conclusion.lang == "en");

	REQUIRE(loaded.conclusion.notes.size() == 1);
	CHECK(loaded.conclusion.notes[0].subject == "Subject");
	CHECK(loaded.conclusion.notes[0].text == "A note");
}

TEST_CASE("Saving a document again updates it instead of adding a second row") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Document d = makeDocument("D1", "First text");
	d.conclusion.notes.push_back({ "", "", "Old note" });
	db.save(d, "S1");

	d.text = "Second text";
	d.extracted = false;
	d.conclusion.notes.clear();
	d.conclusion.notes.push_back({ "", "", "New note" });
	db.save(d, "S1");

	auto docs = db.loadAllDocuments();
	REQUIRE(docs.size() == 1);
	CHECK(docs[0].text == "Second text");
	CHECK_FALSE(docs[0].extracted);

	REQUIRE(docs[0].conclusion.notes.size() == 1);
	CHECK(docs[0].conclusion.notes[0].text == "New note");
}

TEST_CASE("The same local document id in two sources is two documents") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");

	db.save(makeDocument("D1", "From S1"), "S1");
	db.save(makeDocument("D1", "From S2"), "S2");

	CHECK(db.loadAllDocuments().size() == 2);

	auto s1 = db.loadDocumentsForSource("S1");
	REQUIRE(s1.size() == 1);
	CHECK(s1[0].id == "S1:D1");
	CHECK(s1[0].text == "From S1");

	auto s2 = db.loadDocumentsForSource("S2");
	REQUIRE(s2.size() == 1);
	CHECK(s2[0].id == "S2:D1");
	CHECK(s2[0].text == "From S2");
}

TEST_CASE("A source with no documents loads an empty list") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	CHECK(db.loadDocumentsForSource("S1").empty());
}

TEST_CASE("A document cannot be saved into a source that does not exist") {
	storage::Database db(":memory:");
	CHECK_THROWS(db.save(makeDocument("D1", "Orphan"), "S_MISSING"));
}