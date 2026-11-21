#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "storage/database.h"
#include "storage/schema_version.h"
#include "test_helpers.h"

namespace {
	std::string tempPath(const std::string& name) {
		auto p = std::filesystem::temp_directory_path() / name;
		std::filesystem::remove(p);
		return p.string();
	}

	void makeFileWithVersion(const std::string& path, int version) {
		sqlite3* raw = nullptr;
		REQUIRE(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
		std::string sql = "CREATE TABLE persons (id TEXT); PRAGMA user_version = " + std::to_string(version) + ";";
		sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr);
		sqlite3_close(raw);
	}

	int versionOf(const std::string& path) {
		sqlite3* raw = nullptr;
		sqlite3_open(path.c_str(), &raw);
		int v = -1;
		sqlite3_stmt* s = nullptr;
		sqlite3_prepare_v2(raw, "PRAGMA user_version;", -1, &s, nullptr);
		if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int(s, 0);
		sqlite3_finalize(s);
		sqlite3_close(raw);
		return v;
	}

	std::string openError(const std::string& path) {
		try { storage::Database db(path); }
		catch (const std::exception& e) { return e.what(); }
		return "";
	}
}

TEST_CASE("A new project file is stamped with the current schema version") {
	const auto path = tempPath("whp_version_new.db");
	{ storage::Database db(path); }
	CHECK(versionOf(path) == storage::kSchemaVersion);
	std::filesystem::remove(path);
}

TEST_CASE("A project below the minimum supported version is refused") {
	const auto path = tempPath("whp_version_old.db");
	makeFileWithVersion(path, storage::kMinSupportedVersion - 1);

	const std::string error = openError(path);
	CHECK(error.find("pre-release") != std::string::npos);

	CHECK(std::filesystem::remove(path));  // fails if the handle leaked and the file is still locked
}

TEST_CASE("A project from a newer project is refused") {
	const auto path = tempPath("whp_version_new_er.db");
	makeFileWithVersion(path, storage::kSchemaVersion + 1);

	const std::string error = openError(path);
	CHECK(error.find("newer version") != std::string::npos);
	CHECK(std::filesystem::remove(path));
}

TEST_CASE("Reopening a current project keeps its data") {
	const auto path = tempPath("whp_version_reopen.db");
	{
		storage::Database db(path);
		testutil::saveSource(db, "S1");
		gedcomx::Person p;
		p.id = "P1";
		db.save(p, "S1");
	}
	{
		storage::Database db(path);
		CHECK(db.loadAll().size() == 1);
	}
	CHECK(versionOf(path) == storage::kSchemaVersion);
	std::filesystem::remove(path);
}