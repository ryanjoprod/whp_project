#include <doctest/doctest.h>

#include <stdexcept>
#include <set>

#include "storage/database.h"
#include "test_helpers.h"
#include "gedcomx/parser.h"

namespace {
	gedcomx::Citation makeCitation(const std::string& id, const std::string& sourceId, const std::string& page) {
		gedcomx::Citation c;
		c.id = id;
		c.sourceId = sourceId;
		c.page = page;
		return c;
	}
}

TEST_CASE("A citation round-trips with its date, confidence and notes") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Citation c = makeCitation("C1", "S1", "20: Genesis 11:24");
	c.date.original = "5 May 1900";
	c.date.formal = "+1900-05-05";
	c.confidence = "http://gedcomx.org/High";
	c.notes.push_back({ "en", "Transcription", "Reads 'Nahor' in the margin." });
	c.notes.push_back({ "en", "", "Second note" });
	db.save(c);

	auto loaded = db.loadCitation("C1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->sourceId == "S1");
	CHECK(loaded->page == "20: Genesis 11:24");
	CHECK(loaded->date.original == "5 May 1900");
	CHECK(loaded->date.formal == "+1900-05-05");
	CHECK(loaded->confidence == "http://gedcomx.org/High");
	REQUIRE(loaded->notes.size() == 2);
	CHECK(loaded->notes[0].subject == "Transcription");
	CHECK(loaded->notes[0].text == "Reads 'Nahor' in the margin.");  // apostrophes survive
}

TEST_CASE("Saving a citation again replaces it, including its notes") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Citation c = makeCitation("C1", "S1", "page 1");
	c.notes.push_back({ "en", "", "first" });
	c.notes.push_back({ "en", "", "second" });
	db.save(c);

	c.page = "page 2";
	c.notes.clear();
	c.notes.push_back({ "en", "", "only" });
	db.save(c);

	auto loaded = db.loadCitation("C1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->page == "page 2");
	REQUIRE(loaded->notes.size() == 1);  // replaced, not appended
	CHECK(loaded->notes[0].text == "only");
}

TEST_CASE("loadCitationsForSource returns only that source's citations") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	db.save(makeCitation("C2", "S1", "b"));
	db.save(makeCitation("C1", "S1", "a"));
	db.save(makeCitation("C3", "S2", "c"));

	auto s1 = db.loadCitationsForSource("S1");
	REQUIRE(s1.size() == 2);
	CHECK(s1[0].id == "C1");  // ordered by id
	CHECK(s1[1].id == "C2");
	CHECK(db.loadCitationsForSource("S2").size() == 1);
	CHECK(db.loadCitationsForSource("NOPE").empty());
}

TEST_CASE("A missing citation loads as nothing") {
	storage::Database db(":memory:");
	CHECK_FALSE(db.loadCitation("C1").has_value());
}

TEST_CASE("A citation for a source that doesn't exist is rejected, notes and all") {
	storage::Database db(":memory:");
	gedcomx::Citation c = makeCitation("C1", "NO_SUCH_SOURCE", "p");
	c.notes.push_back({ "en", "", "note" });

	CHECK_THROWS_AS(db.save(c), std::runtime_error);
	CHECK_FALSE(db.loadCitation("C1").has_value());
}

TEST_CASE("Apostrophes in citation and source ids do not break storage") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S'1");

	db.save(makeCitation("C'1", "S'1", "O'Brien, p. 4"));

	auto loaded = db.loadCitation("C'1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->page == "O'Brien, p. 4");
	CHECK(db.loadCitationsForSource("S'1").size() == 1);
}

TEST_CASE("isBare recognizes only a source's own bare citation") {
	gedcomx::Citation bare;
	bare.id = gedcomx::bareCitationId("S1");
	bare.sourceId = "S1";
	CHECK(gedcomx::isBare(bare));

	gedcomx::Citation real;
	real.id = "C_1";
	real.sourceId = "S1";
	CHECK_FALSE(gedcomx::isBare(real));

	gedcomx::Citation wrongSource;  // right id shape, wrong source
	wrongSource.id = gedcomx::bareCitationId("S1");
	wrongSource.sourceId = "S2";
	CHECK_FALSE(gedcomx::isBare(wrongSource));
}

TEST_CASE("ensureBareCitation creates one bare citation per source, repeatedly") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	auto first = db.ensureBareCitation("S1");
	auto second = db.ensureBareCitation("S1");
	CHECK(first == "CB_S1");
	CHECK(second == first);

	auto list = db.loadCitationsForSource("S1");
	REQUIRE(list.size() == 1);
	CHECK(gedcomx::isBare(list[0]));
	CHECK(list[0].page.empty());
}

TEST_CASE("ensureBareCitation leaves real citations alone") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	db.save(makeCitation("C1", "S1", "page 4"));

	db.ensureBareCitation("S1");

	CHECK(db.loadCitation("C1")->page == "page 4");
	CHECK(db.loadCitationsForSource("S1").size() == 2);
}

TEST_CASE("ensureBareCitation for an unknown source is rejected") {
	storage::Database db(":memory:");
	CHECK_THROWS_AS(db.ensureBareCitation("NO_SUCH_SOURCE"), std::runtime_error);
}

TEST_CASE("A bare citation id survives apostrophe in the source id") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S'1");

	CHECK(db.ensureBareCitation("S'1") == "CB_S'1");
	CHECK(db.loadCitation("CB_S'1").has_value());
}

TEST_CASE("bareCitationSourceId inverts bareCitationId") {
	CHECK(gedcomx::bareCitationSourceId(gedcomx::bareCitationId("S1")) == "S1");
	CHECK_FALSE(gedcomx::bareCitationSourceId("C_1").has_value());
}

TEST_CASE("isBlankCitation: only details make a citation real") {
	gedcomx::Citation c;
	c.id = "C_1";
	c.sourceId = "S1";
	CHECK(gedcomx::isBlankCitation(c));

	c.page = "p. 12";
	CHECK_FALSE(gedcomx::isBlankCitation(c));
	c.page.clear();

	c.date.original = "1900";
	CHECK_FALSE(gedcomx::isBlankCitation(c));
	c.date.original.clear();

	c.confidence = "http://gedcomx.org/High";
	CHECK_FALSE(gedcomx::isBlankCitation(c));
	c.confidence.clear();

	c.notes.push_back({ "", "", "a note" });
	CHECK_FALSE(gedcomx::isBlankCitation(c));
}

TEST_CASE("citationSummary picks the most useful short text") {
	gedcomx::Citation c;
	c.id = "C_1";
	c.sourceId = "S1";
	CHECK(gedcomx::citationSummary(c) == "(no details)");
	c.date.original = "1900";
	CHECK(gedcomx::citationSummary(c) == "1900");
	c.page = "p. 12";
	CHECK(gedcomx::citationSummary(c) == "p. 12");

	gedcomx::Citation bare;
	bare.sourceId = "S1";
	bare.id = gedcomx::bareCitationId("S1");
	CHECK(gedcomx::citationSummary(bare) == "whole source");
}

TEST_CASE("loadAllCitations returns every citation, bare one included, with notes") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");

	db.ensureBareCitation("S1");

	gedcomx::Citation c;
	c.id = "C_1";
	c.sourceId = "S2";
	c.page = "p. 3";
	c.notes.push_back({ "", "", "checked twice" });
	db.save(c);

	auto all = db.loadAllCitations();
	REQUIRE(all.size() == 2);
	CHECK(all[0].id == "CB_S1");  // ordered by source id, then id
	CHECK(all[1].id == "C_1");
	REQUIRE(all[1].notes.size() == 1);
	CHECK(all[1].notes[0].text == "checked twice");
}

TEST_CASE("gatherSourceExport includes the source's citations and what its records cite elsewhere") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	testutil::saveSource(db, "S3");
	db.ensureBareCitation("S1");
	auto wholeS2 = db.ensureBareCitation("S2");

	gedcomx::Citation own;
	own.id = "C_OWN";
	own.sourceId = "S1";
	own.page = "p. 1";
	db.save(own);

	gedcomx::Citation foreign;
	foreign.id = "C_FOREIGN";
	foreign.sourceId = "S3";
	foreign.page = "p. 9";
	db.save(foreign);

	gedcomx::Citation unrelated;
	unrelated.id = "C_UNRELATED";
	unrelated.sourceId = "S3";
	unrelated.page = "p. 5";
	db.save(unrelated);  // belongs to S3 and nobody here cites it

	gedcomx::Person p;
	p.id = "P1";
	p.conclusion.sources.push_back({ wholeS2 });  // cites S2 as a whole

	gedcomx::Fact birth;
	birth.id = "F1";
	birth.type = "http://gedcomx.org/Birth";
	birth.conclusion.sources.push_back({ "C_FOREIGN" });  // cites a specific place in S3

	p.facts.push_back(birth);
	db.save(p, "S1");

	auto data = db.gatherSourceExport("S1");

	std::set<std::string> citationIds;
	for (const auto& c : data.citations) citationIds.insert(c.id);
	const std::set<std::string> expectedCitations = { "C_OWN", "C_FOREIGN" };  // no bare ones, no C_UNRELATED
	CHECK(citationIds == expectedCitations);

	std::set<std::string> otherIds;
	for (const auto& s : data.otherSources) otherIds.insert(s.id);
	const std::set<std::string> exptectedOthers = { "S2", "S3" };
	CHECK(otherIds == exptectedOthers);
}

TEST_CASE("importCitation refuses an id that already belongs to another source") {
	storage::Database db(":memory:");
	
	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");

	gedcomx::Citation c;
	c.id = "C1";
	c.sourceId = "S1";
	c.page = "p. 1";
	db.importCitations(c);

	c.page = "p. 2";
	db.importCitations(c);  // same id, same source: an ordinary re-import
	auto loaded = db.loadCitation("C1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->page == "p. 2");

	gedcomx::Citation clash;
	clash.id = "C1";
	clash.sourceId = "S2";
	CHECK_THROWS_AS(db.importCitations(clash), std::runtime_error);
	loaded = db.loadCitation("C1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->sourceId == "S1");  // untouched
}

TEST_CASE("A source with citations survives export and import into a fresh project") {
	storage::Database original(":memory:");
	testutil::saveSource(original, "S1");
	testutil::saveSource(original, "S2");
	original.ensureBareCitation("S1");
	auto wholeS2 = original.ensureBareCitation("S2");

	gedcomx::Citation c;
	c.id = "C_1";
	c.sourceId = "S1";
	c.page = "p. 12";
	c.notes.push_back({ "", "", "checked twice" });
	original.save(c);

	gedcomx::Person p;
	p.id = "P1";
	p.conclusion.sources.push_back({ "C_1" });
	p.conclusion.sources.push_back({ wholeS2 });  // a whole-source citation of another source
	p.gender = gedcomx::Gender{ "http://gedcomx.org/Male" };
	original.save(p, "S1");

	// Export S1 the way the app does
	gedcomx::SourceDescription s1;
	for (const auto& s : original.loadAllSourceDescriptions())
		if (s.id == "S1") s1 = s;

	auto data = original.gatherSourceExport("S1");
	auto doc = gedcomx::serializeSourceExport(s1, data.persons, data.relationships, data.otherSources, data.citations);

	// Import it into an empty project, in the app's order
	storage::Database fresh(":memory:");
	auto sources = gedcomx::parseSourceDescriptions(doc);
	auto citations = gedcomx::parseCitations(doc);
	auto people = gedcomx::parsePersons(doc);
	auto relationships = gedcomx::parseRelationships(doc);
	gedcomx::resolveCitationReferences(people, relationships, citations);

	fresh.runInTransaction([&] {
		for (const auto& s : sources) {
			fresh.save(s);
			fresh.ensureBareCitation(s.id);
		}
		for (const auto& cit : citations) fresh.importCitations(cit);
		for (const auto& person : people) fresh.save(person, "S1");
		for (const auto& rel : relationships) fresh.save(rel, "S1");
	});

	auto loadedPerson = fresh.loadPerson("S1:P1");
	REQUIRE(loadedPerson.has_value());
	REQUIRE(loadedPerson->gender.has_value());
	CHECK(loadedPerson->gender->type == "http://gedcomx.org/Male");
	std::set<std::string> refs;
	for (const auto& r : loadedPerson->conclusion.sources) refs.insert(r.citationId);
	const std::set<std::string> expectedRefs = { "C_1", wholeS2 };
	CHECK(refs == expectedRefs);

	auto loadedCitation = fresh.loadCitation("C_1");
	REQUIRE(loadedCitation.has_value());
	CHECK(loadedCitation->sourceId == "S1");
	CHECK(loadedCitation->page == "p. 12");
	REQUIRE(loadedCitation->notes.size() == 1);
	CHECK(loadedCitation->notes[0].text == "checked twice");
}

TEST_CASE("Source details and notes round-trip, and notes are replaced, not appended") {
	storage::Database db(":memory:");

	gedcomx::SourceDescription s;
	s.id = "S1";
	s.titles.push_back({ "", "JPS Hebrew-English Tanakh" });
	s.author = "Various";
	s.publication = "The Jewish Publication Society 1999";
	s.abbreviation = "JPS";
	s.notes.push_back({ "en", "", "First note" });
	s.notes.push_back({ "", "Edition", "Second note" });
	db.save(s);

	auto find = [&](const std::string& id) {
		for (auto& src : db.loadAllSourceDescriptions())
			if (src.id == id) return src;

		return gedcomx::SourceDescription{};
	};

	auto loaded = find("S1");
	CHECK(loaded.primaryTitle() == "JPS Hebrew-English Tanakh");
	CHECK(loaded.author == "Various");
	CHECK(loaded.publication == "The Jewish Publication Society 1999");
	CHECK(loaded.abbreviation == "JPS");
	REQUIRE(loaded.notes.size() == 2);
	CHECK(loaded.notes[0].text == "First note");
	CHECK(loaded.notes[1].subject == "Edition");

	s.notes.pop_back();  // the editor removed a note and cleared the author
	s.author.clear();
	db.save(s);

	loaded = find("S1");
	CHECK(loaded.author.empty());
	REQUIRE(loaded.notes.size() == 1);
	CHECK(loaded.notes[0].text == "First note");
}

TEST_CASE("A citation's calendar, quality and sort value round-trip") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Citation c = makeCitation("C1", "S1", "p. 4");
	c.date = testutil::makeHebrewDate();
	db.save(c);

	auto loaded = db.loadCitation("C1");
	REQUIRE(loaded.has_value());
	testutil::checkHebrewDate(loaded->date);
	CHECK(loaded->page == "p. 4");  // the columns before the date weren't disturbed
}

TEST_CASE("Every citation laoder returns the full date") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Citation c = makeCitation("C1", "S1", "p. 4");
	c.date = testutil::makeHebrewDate();
	db.save(c);

	auto forSource = db.loadCitationsForSource("S1");
	REQUIRE(forSource.size() == 1);
	testutil::checkHebrewDate(forSource[0].date);

	bool found = false;
	for (const auto& each : db.loadAllCitations()) {
		if (each.id != "C1") continue;
		found = true;
		testutil::checkHebrewDate(each.date);
	}
	CHECK(found);
}

TEST_CASE("re-saving a citation replaces its date, including clearing the sort value") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");

	gedcomx::Citation c = makeCitation("C1", "S1", "p. 4");
	c.date = testutil::makeHebrewDate();
	db.save(c);

	c.date = gedcomx::Date{};  // the user clears the date
	db.save(c);

	auto loaded = db.loadCitation("C1");
	REQUIRE(loaded.has_value());
	CHECK(loaded->date.original == "");
	CHECK(loaded->date.calendar == "");
	CHECK(loaded->date.quality == "");
	CHECK_FALSE(loaded->date.sortValue.has_value());
}

TEST_CASE("gatherSourceExport include what an event and its roles cite elsewhere") {
	storage::Database db(":memory:");
	testutil::saveSource(db, "S1");
	testutil::saveSource(db, "S2");
	db.ensureBareCitation("S1");
	auto wholeS2 = db.ensureBareCitation("S2");

	gedcomx::Person p;
	p.id = "P1";
	db.save(p, "S1");

	gedcomx::Event e;
	e.id = "E1";
	e.type = "http://gedcomx.org/Baptism";
	gedcomx::EventRole role;
	role.personId = "P1";
	role.type = "http://gedcomx.org/Principal";
	role.conclusion.sources.push_back({ wholeS2 });
	e.roles.push_back(role);
	db.save(e, "S1");

	auto data = db.gatherSourceExport("S1");
	REQUIRE(data.otherSources.size() == 1);
	CHECK(data.otherSources[0].id == "S2");
}

TEST_CASE("Events and their roles survive export and import into a fresh project") {
	storage::Database original(":memory:");
	testutil::saveSource(original, "S1");
	original.ensureBareCitation("S1");

	gedcomx::Citation c;
	c.id = "C_1";
	c.sourceId = "S1";
	c.page = "p. 3";
	original.save(c);

	for (const char* id : { "P1", "P2" }) {
		gedcomx::Person p;
		p.id = id;
		original.save(p, "S1");
	}

	gedcomx::Event e;
	e.id = "E1";
	e.type = "http://gedcomx.org/Baptism";
	e.date = testutil::makeHebrewDate();
	e.place.original = "Jerusalem";
	e.description = "Naming ceremony";
	e.conclusion.sources.push_back({ "C_1" });
	gedcomx::EventRole principal;
	principal.personId = "P1";
	principal.type = "http://gedcomx.org/Principal";
	gedcomx::EventRole witness;
	witness.personId = "P2";
	witness.type = "http://gedcomx.org/Witness";
	e.roles.push_back(principal);
	e.roles.push_back(witness);
	original.save(e, "S1");

	gedcomx::SourceDescription s1;
	for (const auto& s : original.loadAllSourceDescriptions())
		if (s.id == "S1") s1 = s;

	auto data = original.gatherSourceExport("S1");
	REQUIRE(data.events.size() == 1);
	CHECK(data.events[0].id == "E1");  // prefix stripped for the file
	REQUIRE(data.events[0].roles.size() == 2);
	CHECK(data.events[0].roles[0].personId == "P1");
	auto doc = gedcomx::serializeSourceExport(s1, data.persons, data.relationships, data.otherSources, data.citations, data.events);

	storage::Database fresh(":memory:");
	auto sources = gedcomx::parseSourceDescriptions(doc);
	auto citations = gedcomx::parseCitations(doc);
	auto people = gedcomx::parsePersons(doc);
	auto relationships = gedcomx::parseRelationships(doc);
	auto events = gedcomx::parseEvents(doc);
	gedcomx::resolveCitationReferences(people, relationships, events, citations);

	fresh.runInTransaction([&] {
		for (const auto& s : sources) {
			fresh.save(s);
			fresh.ensureBareCitation(s.id);
		}
		for (const auto& cit : citations) fresh.importCitations(cit);
		for (const auto& person : people) fresh.save(person, "S1");
		for (const auto& rel : relationships) fresh.save(rel, "S1");
		for (const auto& ev : events) fresh.save(ev, "S1");
	});

	auto loaded = fresh.loadEventsForSource("S1");
	REQUIRE(loaded.size() == 1);
	CHECK(loaded[0].id == "S1:E1");
	CHECK(loaded[0].type == "http://gedcomx.org/Baptism");
	testutil::checkHebrewDate(loaded[0].date);
	CHECK(loaded[0].place.original == "Jerusalem");
	CHECK(loaded[0].description == "Naming ceremony");
	REQUIRE(loaded[0].conclusion.sources.size() == 1);
	CHECK(loaded[0].conclusion.sources[0].citationId == "C_1");  // remapped, not "CB_C_1"
	REQUIRE(loaded[0].roles.size() == 2);
	CHECK(loaded[0].roles[0].personId == "S1:P1");
	CHECK(loaded[0].roles[1].personId == "S1:P2");
	CHECK(loaded[0].roles[1].type == "http://gedcomx.org/Witness");
}

TEST_CASE("Source attributes round-trip in order aand are replaced, not appended") {
	storage::Database db(":memory:");

	gedcomx::SourceDescription s;
	s.id = "S1";
	s.titles.push_back({ "", "Source S1" });
	s.attributes.push_back({ "", "Call number", "929.1 SMI" });
	s.attributes.push_back({ "", "Repository", "State Archive" });
	db.save(s);

	auto loaded = db.loadAllSourceDescriptions();
	REQUIRE(loaded.size() == 1);
	REQUIRE(loaded[0].attributes.size() == 2);
	CHECK(loaded[0].attributes[0].type == "Call number");
	CHECK(loaded[0].attributes[0].value == "929.1 SMI");
	CHECK(loaded[0].attributes[1].type == "Repository");

	s.attributes.pop_back();
	s.attributes[0].value = "929.1 SMI rev.";
	db.save(s);

	loaded = db.loadAllSourceDescriptions();
	REQUIRE(loaded.size() == 1);
	REQUIRE(loaded[0].attributes.size() == 1);
	CHECK(loaded[0].attributes[0].value == "929.1 SMI rev.");
}

TEST_CASE("Citation attributes round-trip through every loader and are replaced on re-save") {
	storage::Database db(":memory:");

	testutil::saveSource(db, "S1");

	gedcomx::Citation c = makeCitation("C1", "S1", "p. 4");
	c.attributes.push_back({ "", "Film number", "1234" });
	c.attributes.push_back({ "", "Item", "7" });
	db.save(c);

	auto one = db.loadCitation("C1");
	REQUIRE(one.has_value());
	REQUIRE(one->attributes.size() == 2);
	CHECK(one->attributes[0].type == "Film number");
	CHECK(one->attributes[1].value == "7");

	auto forSource = db.loadCitationsForSource("S1");
	REQUIRE(forSource.size() == 1);
	CHECK(forSource[0].attributes.size() == 2);

	size_t fromAll = 0;
	for (const auto& each : db.loadAllCitations())
		if (each.id == "C1") fromAll = each.attributes.size();
	CHECK(fromAll == 2);

	c.attributes.pop_back();
	db.save(c);
	auto again = db.loadCitation("C1");
	REQUIRE(again.has_value());
	CHECK(again->attributes.size() == 1);
}

TEST_CASE("A citation with only an attribute is not blank") {
	gedcomx::Citation c;
	CHECK(gedcomx::isBlankCitation(c));
	c.attributes.push_back({ "", "Film number", "1234" });
	CHECK_FALSE(gedcomx::isBlankCitation(c));
}

TEST_CASE("Source and citation attributes survive export and import into a fresh project") {
	storage::Database original(":memory:");
	gedcomx::SourceDescription s1;
	s1.id = "S1";
	s1.titles.push_back({ "", "Source S1" });
	s1.attributes.push_back({ "", "Call number", "929.1 SMI" });
	original.save(s1);
	original.ensureBareCitation("S1");

	gedcomx::Citation c = makeCitation("C_1", "S1", "p. 12");
	c.attributes.push_back({ "", "Film number", "1234" });
	original.save(c);

	gedcomx::Person p;
	p.id = "P1";
	p.conclusion.sources.push_back({ "C_1" });
	original.save(p, "S1");

	gedcomx::SourceDescription toExport;
	for (const auto& s : original.loadAllSourceDescriptions())
		if (s.id == "S1") toExport = s;

	auto data = original.gatherSourceExport("S1");
	auto doc = gedcomx::serializeSourceExport(toExport, data.persons, data.relationships, data.otherSources, data.citations, data.events);

	storage::Database fresh(":memory:");
	auto sources = gedcomx::parseSourceDescriptions(doc);
	auto citations = gedcomx::parseCitations(doc);
	auto people = gedcomx::parsePersons(doc);
	auto relationships = gedcomx::parseRelationships(doc);
	gedcomx::resolveCitationReferences(people, relationships, citations);

	fresh.runInTransaction([&] {
		for (const auto& s : sources) {
			fresh.save(s);
			fresh.ensureBareCitation(s.id);
		}
		for (const auto& cit : citations) fresh.importCitations(cit);
		for (const auto& person : people) fresh.save(person, "S1");
	});

	auto loadedSources = fresh.loadAllSourceDescriptions();
	REQUIRE(loadedSources.size() == 1);
	REQUIRE(loadedSources[0].attributes.size() == 1);
	CHECK(loadedSources[0].attributes[0].type == "Call number");

	auto loadedCitation = fresh.loadCitation("C_1");
	REQUIRE(loadedCitation.has_value());
	REQUIRE(loadedCitation->attributes.size() == 1);
	CHECK(loadedCitation->attributes[0].value == "1234");
}