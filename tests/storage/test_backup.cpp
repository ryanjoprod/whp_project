#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "test_helpers.h"
#include "storage/database.h"
#include "gedcomx/person.h"

namespace fs = std::filesystem;

namespace {
	fs::path freshTestFolder() {
		const fs::path dir = fs::temp_directory_path() / "whp_backup_test";
		fs::remove_all(dir);
		fs::create_directories(dir);
		return dir;
	}
}

TEST_CASE("backupTo writes a copy that opens with the same data") {
	const fs::path dir = freshTestFolder();
	const std::string original = (dir / "original.db").string();
	const std::string copy = (dir / "copy.db").string();

	{
		storage::Database db(original);
		testutil::saveSource(db, "S1");

		gedcomx::Person person;
		person.id = "P1";
		gedcomx::Name name;
		gedcomx::NameForm form;
		form.fullText = "Ada Lovelace";
		name.nameForms.push_back(form);
		person.names.push_back(name);
		db.save(person, "S1");
		db.backupTo(copy);  // the original stays open and usable
		CHECK(db.loadAll().size() == 1);
	}

	{
		storage::Database restored(copy);
		auto people = restored.loadAll();
		REQUIRE(people.size() == 1);
		CHECK(people[0].id == "S1:P1");
	}  // closed here, so the folder can be deleted

	fs::remove_all(dir);
}

TEST_CASE("backupTo refuses to overwrite an existing file") {
	const fs::path dir = freshTestFolder();
	const std::string original = (dir / "original.db").string();
	const std::string target = (dir / "target.db").string();

	{
		storage::Database db(original);
		db.backupTo(target);
		CHECK_THROWS(db.backupTo(target));
	}

	fs::remove_all(dir);
}