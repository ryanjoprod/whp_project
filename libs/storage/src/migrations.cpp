#include "storage/migrations.h"
#include "storage/schema_version.h"

namespace storage {
	MigrationPlan defaultMigrationPlan() {
		MigrationPlan plan;
		plan.targetVersion = kSchemaVersion;

		// v9 is the baseline, so there are no steps yet. A future step looks like:
		// plan.steps.push_back({ 10, false, [](const SqlRunner& sql) {
		//     sql("ALTER TABLE places ADD COLUMN something TEXT;");
		// } });

		return plan;
	}
}