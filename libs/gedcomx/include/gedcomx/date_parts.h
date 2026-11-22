#pragma once

#include <optional>
#include <string>

#include "gedcomx/fact.h"  // for Date

namespace gedcomx {
	enum class DateKind { Regular, Before, After, About, Range, Span, TextOnly };

	// One calendar date. 0 means "not given". A negative year is BC (there is no year 0).
	struct SimpleDate {
		int year = 0;
		int month = 0;
		int day = 0;
	};

	// What the Date editor edits; Date is what gets stored.
	struct DateParts {
		DateKind kind = DateKind::Regular;
		std::string calendar;  // "" or "Gregorian" = Gregorian; "Julian", "Hebrew", ...
		std::string quality;  // "", "estimated", "calculated"
		SimpleDate start;
		SimpleDate stop;  // Range and Span only
		std::string text;  // TextOnly: the words as written
	};

	// "" = fine, otherwise a sentence the editor can show
	std::string validateDateParts(const DateParts& parts);

	// Julian Day Number of the day (missing month/day count as 1). Only Gregorian and Julian so far;
	// nullopt for anything else, or when there is no year.
	std::optional<long long> julianDayNumber(const SimpleDate& date, const std::string& calendar);

	std::string displayText(const DateParts& parts);
	std::string formalDate(const DateParts& parts);

	Date toDate(const DateParts& parts);
	DateParts fromDate(const Date& date);
}