#pragma once

#include <functional>
#include <string>
#include <vector>

namespace storage {
	// Runs one SQl string inside the step's transaction
	using SqlRunner = std::function<void(const std::string& sql)>;

	struct Migration {
		int toVersion = 0;  // the version this step produces
		bool needsForeignKeysOff = false;  // table rebuilds: dropping a parent would cascade-delete its children
		std::function<void(const SqlRunner&)> apply;
	};

	struct MigrationPlan {
		int targetVersion = 0;
		std::vector<Migration> steps;  // one step per version, e.g. 10, 11, 12...
	};

	MigrationPlan defaultMigrationPlan();
}