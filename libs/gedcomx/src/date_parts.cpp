#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "gedcomx/date_parts.h"

namespace gedcomx {
	namespace {
		const char* const kMonthNames[12] = {
			"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"
		};

		bool isGregorian(const std::string& calendar) {
			return calendar.empty() || calendar == "Gregorian";
		}
		bool isJulian(const std::string& calendar) {
			return calendar == "Julian";
		}
		bool isEmptySimple(const SimpleDate& d) {
			return d.year == 0 && d.month == 0 && d.day == 0;
		}

		// There is no year 0 in what people type (1BC is followed by AD 1), but the maths need one.
		int astronomicalYear(int year) {
			return year < 0 ? year + 1 : year;
		}

		bool isLeap(int year, const std::string& calendar) {
			const int y = astronomicalYear(year);
			if (isJulian(calendar)) return y % 4 == 0;
			return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
		}

		int daysInMonth(int year, int month, const std::string& calendar) {
			static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
			if (month == 2 && isLeap(year, calendar)) return 29;
			return days[month - 1];
		}

		std::string pad(int value, int width) {
			std::string s = std::to_string(value);
			while ((int)s.size() < width)
				s = "0" + s;
			return s;
		}

		std::string formalOne(const SimpleDate& d) {
			std::string s = d.year < 0 ? "-" : "+";
			s += pad(std::abs(d.year), 4);
			if (d.month != 0) {
				s += "-" + pad(d.month, 2);
				if (d.day != 0) s += "-" + pad(d.day, 2);
			}
			return s;
		}

		std::string textOne(const SimpleDate& d) {
			std::string s;
			if (d.day != 0) s += std::to_string(d.day) + " ";
			if (d.month != 0) {
				s += (d.month >= 1 && d.month <= 12)
					? kMonthNames[d.month - 1]
					: "month " + std::to_string(d.month);
				s += " ";
			}
			s += std::to_string(std::abs(d.year));
			if (d.year < 0)
				s += " BC";
			return s;
		}

		// "+1900-05-05" / "-0044-03" / "+1900T12:00" -> SimpleDate. A time part is ignored.
		bool parseOne(const std::string& s, SimpleDate& out) {
			size_t i = 0;
			if (s.empty() || (s[0] != '+' && s[0] != '-')) return false;
			const bool negative = s[i++] == '-';

			auto number = [&](int& value) {
				const size_t begin = i;
				int v = 0;
				while (i < s.size() && std::isdigit((unsigned char)s[i])) {
					v = v * 10 + (s[i] - '0');
					if (v > 100000) return false;
					++i;
				}
				if (i == begin) return false;
				value = v;
				return true;
				};

			int year = 0;
			if (!number(year) || year == 0) return false;

			SimpleDate d;
			d.year = negative ? -year : year;
			if (i < s.size() && s[i] == '-') {
				++i;
				if (!number(d.month)) return false;
				if (i < s.size() && s[i] == '-') {
					++i;
					if (!number(d.day)) return false;
				}
			}
			if (i < s.size() && s[i] != 'T') return false;  // anything else is not a date we understand

			out = d;
			return true;
		}

		// Range and Span both store "X/Y"; the text we generate tells them apart
		bool saysFrom(const std::string& original) {
			std::string s = original;
			for (const char* prefix : { "estimated ", "calculated " }) {
				const std::string p = prefix;
				if (s.compare(0, p.size(), p) == 0) {
					s.erase(0, p.size());
					break;
				}
			}
			return s.compare(0, 5, "from ") == 0;
		}

		std::string checkOne(const SimpleDate& d, const std::string& calendar, const std::string& label) {
			if (d.year == 0)
				return label + ": " + ((d.month != 0 || d.day != 0)
					? "a year is needed."
					: "enter a year.");
			if (d.month < 0)
				return label + ": the month cannot be negative.";
			if (d.day < 0)
				return label + ": the day cannot be negative.";
			if (d.day != 0 && d.month == 0)
				return label + ": a day needs a month.";

			const bool simpleCalendar = isGregorian(calendar) || isJulian(calendar);
			if (simpleCalendar) {
				if (d.month > 12)
					return label + ": the month must be 1 to 12.";
				if (d.month != 0 && d.day > daysInMonth(d.year, d.month, calendar))
					return label + ": " + kMonthNames[d.month - 1] + " has only " + std::to_string(daysInMonth(d.year, d.month, calendar)) + " days that year.";
			}
			else {
				if (d.month > 13)
					return label + ": the month must be 1 to 13.";
				if (d.day > 31)
					return label + ": the day must be 1 to 31.";
			}
			return "";
		}

		bool before(const SimpleDate& a, const SimpleDate& b) {
			if (a.year != b.year)
				return a.year < b.year;
			const int am = std::max(a.month, 1), bm = std::max(b.month, 1);
			if (am != bm) return am < bm;
			return std::max(a.day, 1) < std::max(b.day, 1);
		}
	}

	std::string validateDateParts(const DateParts& parts) {
		if (parts.kind == DateKind::TextOnly) return "";
		if (parts.kind == DateKind::Regular && isEmptySimple(parts.start)) return "";

		std::string error = checkOne(parts.start, parts.calendar, parts.kind == DateKind::Range || parts.kind == DateKind::Span ? "Start" : "Date");
		if (!error.empty()) return error;

		if (parts.kind == DateKind::Range || parts.kind == DateKind::Span) {
			error = checkOne(parts.stop, parts.calendar, "End");
			if (!error.empty()) return error;
			if (before(parts.stop, parts.start))
				return "The end date is before the start date.";
		}
		return "";
	}

	std::optional<long long> julianDayNumber(const SimpleDate& date, const std::string& calendar) {
		if (date.year == 0) return std::nullopt;
		if (!isGregorian(calendar) && !isJulian(calendar)) return std::nullopt;

		const long long y = astronomicalYear(date.year);
		if (y < -4700) return std::nullopt;  // the formula below needs a positive shifted year

		const long long m = date.month == 0 ? 1 : date.month;
		const long long d = date.day == 0 ? 1 : date.day;
		const long long a = (14 - m) / 12;
		const long long yy = y + 4800 - a;
		const long long mm = m + 12 * a - 3;

		long long jdn = d + (153 * mm + 2) / 5 + 365 * yy + yy / 4;
		if (isJulian(calendar)) jdn -= 32083;
		else jdn += -yy / 100 + yy / 400 - 32045;

		return jdn; 
	}

	std::string displayText(const DateParts& parts) {
		if (parts.kind == DateKind::TextOnly) return parts.text;
		if (isEmptySimple(parts.start)) return "";

		std::string s;
		if (parts.quality == "estimated") s = "estimated ";
		else if (parts.quality == "calculated") s = "calculated ";

		switch (parts.kind) {
		case DateKind::Before: s += "before " + textOne(parts.start); break;
		case DateKind::After: s += "after " + textOne(parts.start); break;
		case DateKind::About: s += "about " + textOne(parts.start); break;
		case DateKind::Range: s += "between " + textOne(parts.start) + " and " + textOne(parts.stop); break;
		case DateKind::Span: s += "from " + textOne(parts.start) + " to " + textOne(parts.stop); break;
		default: s += textOne(parts.start); break;
		}

		if (!isGregorian(parts.calendar)) s += " (" + parts.calendar + ")";
		return s;
	}

	std::string formalDate(const DateParts& parts) {
		if (parts.kind == DateKind::TextOnly || isEmptySimple(parts.start)) return "";

		switch (parts.kind) {
		case DateKind::Before: return "/" + formalOne(parts.start);
		case DateKind::After: return formalOne(parts.start) + "/";
		case DateKind::About: return "A" + formalOne(parts.start);
		case DateKind::Range:
		case DateKind::Span: return formalOne(parts.start) + "/" + formalOne(parts.stop);
		default: return formalOne(parts.start);
		}
	}

	Date toDate(const DateParts& parts) {
		Date d;
		if (parts.kind == DateKind::TextOnly) {
			if (parts.text.empty()) return d;
			d.original = parts.text;
			d.calendar = isGregorian(parts.calendar) ? "" : parts.calendar;
			d.quality = parts.quality;
			return d;  // no formal date and no sort value: the words are all we know
		}
		if (isEmptySimple(parts.start)) return d;

		d.calendar = isGregorian(parts.calendar) ? "" : parts.calendar;
		d.quality = parts.quality;
		d.original = displayText(parts);
		d.formal = formalDate(parts);
		d.sortValue = julianDayNumber(parts.start, d.calendar);
		return d;
	}

	DateParts fromDate(const Date& date) {
		DateParts parts;
		parts.calendar = date.calendar;
		parts.quality = date.quality;

		auto textOnly = [&]() {
			parts.kind = DateKind::TextOnly;
			parts.text = date.original;
			parts.start = SimpleDate();
			parts.stop = SimpleDate();
			return parts;
		};

		if (date.formal.empty()) {
			if (date.original.empty()) return parts;  // an empty date
			return textOnly();
		}

		std::string formal = date.formal;
		bool about = false;
		if (formal[0] == 'A') {
			about = true;
			formal.erase(0, 1);
		}

		const size_t slash = formal.find('/');
		if (slash == std::string::npos) {
			SimpleDate d;
			if (!parseOne(formal, d)) return textOnly();
			parts.kind = about ? DateKind::About : DateKind::Regular;
			parts.start = d;
			return parts;
		}

		if (about) return textOnly();  // "A" on a range is not something this editor writes

		const std::string left = formal.substr(0, slash);
		const std::string right = formal.substr(slash + 1);

		if (left.empty() && right.empty()) return textOnly();
		if (left.empty()) {
			SimpleDate d;
			if (!parseOne(right, d)) return textOnly();
			parts.kind = DateKind::Before;
			parts.start = d;
			return parts;
		}
		if (right.empty()) {
			SimpleDate d;
			if (!parseOne(left, d)) return textOnly();
			parts.kind = DateKind::After;
			parts.start = d;
			return parts;
		}

		SimpleDate a, b;
		if (!parseOne(left, a) || !parseOne(right, b)) return textOnly();
		parts.kind = saysFrom(date.original) ? DateKind::Span : DateKind::Range;
		parts.start = a;
		parts.stop = b;
		return parts;
	}
}