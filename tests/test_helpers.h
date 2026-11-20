#pragma once

#include <doctest/doctest.h>

#include <string>

#include "storage/database.h"
#include "gedcomx/fact.h"

namespace testutil {
	inline void saveSource(storage::Database& db, const std::string& id) {
		gedcomx::SourceDescription s;
		s.id = id;
		s.titles.push_back({ "", "Source " + id });
		db.save(s);
	}

	inline std::string saveSourceWithCitation(storage::Database& db, const std::string& sourceId) {
		saveSource(db, sourceId);
		return db.ensureBareCitation(sourceId);
	}

	inline gedcomx::Date makeHebrewDate() {
		gedcomx::Date d;
		d.original = "6 Tishri 5000";
		d.formal = "-3761-10-07";
		d.calendar = "Hebrew";
		d.quality = "estimated";
		d.sortValue = 1234567;

		return d;
	}

	inline void checkHebrewDate(const gedcomx::Date& d) {
		CHECK(d.original == "6 Tishri 5000");
		CHECK(d.formal == "-3761-10-07");
		CHECK(d.calendar == "Hebrew");
		CHECK(d.quality == "estimated");
		REQUIRE(d.sortValue.has_value());
		CHECK(*d.sortValue == 1234567);
	}
}