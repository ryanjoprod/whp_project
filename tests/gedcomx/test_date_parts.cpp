#include <doctest/doctest.h>

#include "gedcomx/date_parts.h"

namespace {
	gedcomx::DateParts regular(int y, int m = 0, int d = 0) {
		gedcomx::DateParts p;
		p.start = { y, m, d };
		return p;
	}
}

TEST_CASE("Julian Day Number match know values") {
	CHECK(*gedcomx::julianDayNumber({ 2000, 1, 1 }, "") == 2451545);
	CHECK(*gedcomx::julianDayNumber({ 1900,1, 1 }, "Gregorian") == 2415021);
	CHECK(*gedcomx::julianDayNumber({ 1999, 12, 19 }, "Julian") == 2451545);  // the same day as 2000-01-01
	CHECK(*gedcomx::julianDayNumber({ 1, 1, 1 }, "Julian") == 1721424);
	CHECK(*gedcomx::julianDayNumber({ -1, 1, 1 }, "Julian") == 1721058);  // 1 BC is year 0: a leap year
}

TEST_CASE("A missing month or day counts as the first") {
	CHECK(*gedcomx::julianDayNumber({ 1900, 5, 0 }, "") == *gedcomx::julianDayNumber({ 1900, 5, 1 }, ""));
	CHECK(*gedcomx::julianDayNumber({ 1900, 0, 0 }, "") == *gedcomx::julianDayNumber({ 1900, 1, 1 }, ""));
}

TEST_CASE("No sort value without a year or for calendars not converted yet") {
	CHECK_FALSE(gedcomx::julianDayNumber({ 0, 5, 5 }, "").has_value());
	CHECK_FALSE(gedcomx::julianDayNumber({ 5760, 1, 1 }, "Hebrew").has_value());
}

TEST_CASE("Validation catch impossible days, months, and orders") {
	CHECK(gedcomx::validateDateParts(regular(1900, 2, 29)) != "");  // 1900 was not a leap year
	CHECK(gedcomx::validateDateParts(regular(2000, 2, 20)) == "");
	CHECK(gedcomx::validateDateParts(regular(1900, 4, 31)) != "");
	CHECK(gedcomx::validateDateParts(regular(1900, 13, 1)) != "");
	CHECK(gedcomx::validateDateParts(regular(1900, 0, 5)) != "");  // a day needs a month
	CHECK(gedcomx::validateDateParts(regular(0, 5, 0)) != "");  // a month needs a year

	auto julianLeap = regular(1900, 2, 29);
	julianLeap.calendar = "Julian";
	CHECK(gedcomx::validateDateParts(julianLeap) == "");  // but it was one in the Julian calendar
}

TEST_CASE("An empty date and text-only are valid; other kinds need a year") {
	CHECK(gedcomx::validateDateParts(gedcomx::DateParts()) == "");

	gedcomx::DateParts text;
	text.kind = gedcomx::DateKind::TextOnly;
	text.text = "Easter 1832";
	CHECK(gedcomx::validateDateParts(text) == "");

	gedcomx::DateParts about;
	about.kind = gedcomx::DateKind::About;
	CHECK(gedcomx::validateDateParts(about) != "");
}

TEST_CASE("A range or span must end after it starts") {
	gedcomx::DateParts p;
	p.kind = gedcomx::DateKind::Range;
	p.start = { 1910, 0, 0 };
	p.stop = { 1900, 0, 0 };
	CHECK(gedcomx::validateDateParts(p) != "");

	p.stop = { 1910, 0, 0 };
	CHECK(gedcomx::validateDateParts(p) == "");  // the same year is allowed

	p.stop = { 0, 0, 0 };
	CHECK(gedcomx::validateDateParts(p) != "");  // an end is required
}

TEST_CASE("Text and formal dates for every kind") {
	gedcomx::DateParts p = regular(1900, 5, 5);
	CHECK(gedcomx::displayText(p) == "5 May 1900");
	CHECK(gedcomx::formalDate(p) == "+1900-05-05");

	p.start = { 1900, 5, 0 };
	CHECK(gedcomx::displayText(p) == "May 1900");
	CHECK(gedcomx::formalDate(p) == "+1900-05");

	p.start = { 1900, 0, 0 };
	p.kind = gedcomx::DateKind::About;
	CHECK(gedcomx::displayText(p) == "about 1900");
	CHECK(gedcomx::formalDate(p) == "A+1900");

	p.kind = gedcomx::DateKind::Before;
	CHECK(gedcomx::displayText(p) == "before 1900");
	CHECK(gedcomx::formalDate(p) == "/+1900");

	p.kind = gedcomx::DateKind::After;
	CHECK(gedcomx::formalDate(p) == "+1900/");

	p.kind = gedcomx::DateKind::Range;
	p.stop = { 1910, 0, 0 };
	CHECK(gedcomx::displayText(p) == "between 1900 and 1910");
	CHECK(gedcomx::formalDate(p) == "+1900/+1910");

	p.kind = gedcomx::DateKind::Span;
	CHECK(gedcomx::displayText(p) == "from 1900 to 1910");
	CHECK(gedcomx::formalDate(p) == "+1900/+1910");
}

TEST_CASE("Quality, calendar and BC show in the text") {
	gedcomx::DateParts p = regular(-44, 3, 15);
	CHECK(gedcomx::displayText(p) == "15 March 44 BC");
	CHECK(gedcomx::formalDate(p) == "-0044-03-15");

	p = regular(1582, 10, 5);
	p.calendar = "Julian";
	p.quality = "estimated";
	CHECK(gedcomx::displayText(p) == "estimated 5 October 1582 (Julian)");
}

TEST_CASE("toDate fills all five fields, and text-only keeps just the words") {
	auto d = gedcomx::toDate(regular(2000, 1, 1));
	CHECK(d.original == "1 January 2000");
	CHECK(d.formal == "+2000-01-01");
	CHECK(d.calendar.empty());
	REQUIRE(d.sortValue.has_value());
	CHECK(*d.sortValue == 2451545);

	gedcomx::DateParts text;
	text.kind = gedcomx::DateKind::TextOnly;
	text.text = "Easter 1832";
	auto t = gedcomx::toDate(text);
	CHECK(t.original == "Easter 1832");
	CHECK(t.formal.empty());
	CHECK_FALSE(t.sortValue.has_value());
}

TEST_CASE("toDate writes Gregorian as an empty calendar, and an empty date as nothing") {
	auto p = regular(1900, 1, 1);
	p.calendar = "Gregorian";
	CHECK(gedcomx::toDate(p).calendar.empty());

	auto empty = gedcomx::toDate(gedcomx::DateParts());
	CHECK(empty.original.empty());
	CHECK(empty.formal.empty());
	CHECK_FALSE(empty.sortValue.has_value());
}

TEST_CASE("Every kind survives toDate and fromDate") {
	using gedcomx::DateKind;
	const DateKind kinds[] = { DateKind::Regular, DateKind::About, DateKind::Before, DateKind::After, DateKind::Range, DateKind::Span };

	for (DateKind kind : kinds) {
		gedcomx::DateParts p;
		p.kind = kind;
		p.start = { 1900, 5, 5 };
		if (kind == DateKind::Range || kind == DateKind::Span) p.stop = { 1910, 6, 0 };
		p.quality = "estimated";
		p.calendar = "Julian";

		auto back = gedcomx::fromDate(gedcomx::toDate(p));

		CHECK(back.kind == kind);
		CHECK(back.start.year == 1900);
		CHECK(back.start.month == 5);
		CHECK(back.start.day == 5);
		CHECK(back.stop.year == p.stop.year);
		CHECK(back.stop.month == p.stop.month);
		CHECK(back.quality == "estimated");
		CHECK(back.calendar == "Julian");
	}
}

TEST_CASE("A date from another program readsd as its kind, or as text when it is not understood") {
	gedcomx::Date standard;
	standard.original = "5 May 1900";
	standard.formal = "+1900-05-05";
	auto p = gedcomx::fromDate(standard);
	CHECK(p.kind == gedcomx::DateKind::Regular);
	CHECK(p.start.month == 5);

	gedcomx::Date bc;
	bc.formal = "-0044-03-15";
	CHECK(gedcomx::fromDate(bc).start.year == -44);

	gedcomx::Date duration;
	duration.original = "for five years";
	duration.formal = "P5Y";
	auto text = gedcomx::fromDate(duration);
	CHECK(text.kind == gedcomx::DateKind::TextOnly);
	CHECK(text.text == "for five years");

	gedcomx::Date wordsOnly;
	wordsOnly.original = "sometime in spring";
	CHECK(gedcomx::fromDate(wordsOnly).kind == gedcomx::DateKind::TextOnly);

	CHECK(gedcomx::fromDate(gedcomx::Date()).kind == gedcomx::DateKind::Regular);  // an empty date
}