#include <doctest/doctest.h>

#include <filesystem>
#include <functional>
#include <string>

#include <sqlite3.h>

#include "storage/database.h"
#include "storage/migrations.h"
#include "storage/schema_version.h"
#include "test_helpers.h"

namespace {
	std::string tempPath(const std::string& name) {
		auto p = std::filesystem::temp_directory_path() / name;
		std::filesystem::remove(p);
		std::filesystem::remove(p.string() + ".v9.bak");
		return p.string();
	}

	int intQuery(const std::string& path, const std::string& sql) {
		sqlite3* raw = nullptr;
		REQUIRE(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
		sqlite3_stmt* s = nullptr;
		REQUIRE(sqlite3_prepare_v2(raw, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
		int v = -1;
		if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int(s, 0);
		sqlite3_finalize(s);
		sqlite3_close(raw);
		return v;
	}

	int versionOf(const std::string& path) {
		return intQuery(path, "PRAGMA user_version;");
	}

	bool hasColumn(const std::string& path, const std::string& table, const std::string& column) {
		return intQuery(path, "SELECT COUNT(*) FROM pragma_table_info('" + table + "') WHERE name = '" + column + "';") > 0;
	}

	void makeV9Project(const std::string& path) {
		storage::Database db(path);
		testutil::saveSource(db, "S1");
	}

	storage::MigrationPlan planTo(int target, int stepVersion, bool fkOff, std::function<void(const storage::SqlRunner&)> apply) {
		storage::MigrationPlan plan;
		plan.targetVersion = target;
		plan.steps.push_back({ stepVersion, fkOff, std::move(apply) });
		return plan;
	}

	std::string openError(const std::string& path, const storage::MigrationPlan& plan) {
		try {
			storage::Database ddb(path, plan);
		}
		catch (const std::exception& e) {
			return e.what();
		}
		return "";
	}

	auto addColumn = [](const storage::SqlRunner& sql) {
		sql("ALTER TABLE sources ADD COLUMN extra TEXT;");
	};
}

TEST_CASE("An older project is upgraded and keeps its data") {
	const auto path = tempPath("whp_mig_upgrade.db");
	makeV9Project(path);

	{
		storage::Database db(path, planTo(10, 10, false, addColumn));
		CHECK(db.loadAllSourceDescriptions().size() == 1);
	}
	CHECK(versionOf(path) == 10);
	CHECK(hasColumn(path, "sources", "extra"));
}

TEST_CASE("An upgrade leaves a backup of the project as it was") {
	const auto path = tempPath("whp_mig_backup.db");
	makeV9Project(path);
	{
		storage::Database db(path, planTo(10, 10, false, addColumn));
	}

	const std::string backup = path + ".v9.bak";
	REQUIRE(std::filesystem::exists(backup));
	CHECK(versionOf(backup) == 9);
	CHECK_FALSE(hasColumn(backup, "sources", "extra"));
	CHECK(intQuery(backup, "SELECT COUNT(*) FROM sources;") == 1);
}

TEST_CASE("A step that fails is rolled back completely") {
	const auto path = tempPath("whp_mig_fail.db");
	makeV9Project(path);

	auto plan = planTo(10, 10, false, [](const storage::SqlRunner& sql) {
		sql("ALTER TABLE sources ADD COLUMN extra TEXT;");
		sql("THIS IS NOT SQL;");
	});
	const std::string error = openError(path, plan);

	CHECK(error.find("Upgrade to database v10 failed") != std::string::npos);
	CHECK(versionOf(path) == 9);
	CHECK_FALSE(hasColumn(path, "sources", "extra"));
	CHECK(std::filesystem::exists(path + ".v9.bak"));
}

TEST_CASE("A missingstep is refused before anything is touched") {
	const auto path = tempPath("whp_mig_gap.db");
	makeV9Project(path);

	const std::string error = openError(path, planTo(11, 10, false, addColumn));  // no step to 11

	CHECK(error.find("No upgrade path") != std::string::npos);
	CHECK(versionOf(path) == 9);
	CHECK_FALSE(std::filesystem::exists(path + ".v9.bak"));
}

TEST_CASE("A table-rebuild step that breaks a foreign key is refused") {
	const auto path = tempPath("whp_mig_fk.db");
	makeV9Project(path);

	auto plan = planTo(10, 10, true, [](const storage::SqlRunner& sql) {
		sql("INSERT INTO person_links (person_id, aggregate_id) VALUES ('NOPE:P1', 'AGG1');");
	});
	const std::string error = openError(path, plan);

	CHECK(error.find("foreign key") != std::string::npos);
	CHECK(versionOf(path) == 9);
	CHECK(intQuery(path, "SELECT COUNT(*) FROM person_links;") == 0);
}

TEST_CASE("Foreign keys are switched back on after a rebuild step") {
	const auto path = tempPath("whp_mig_fkon.db");
	makeV9Project(path);

	storage::Database db(path, planTo(10, 10, true, addColumn));
	gedcomx::Person p;
	p.id = "P1";
	CHECK_THROWS(db.save(p, "S_MISSING"));  // would succeed if foreign keys were still off
}

TEST_CASE("Opening a project that is already current makes no backup") {
	const auto path = tempPath("whp_mig_current.db");
	makeV9Project(path);
	{
		storage::Database db(path);
	}

	CHECK_FALSE(std::filesystem::exists(path + ".v9.bak"));
	CHECK(versionOf(path) == storage::kSchemaVersion);
}