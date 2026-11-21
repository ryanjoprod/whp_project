# WHP Program - Schema Versions and Migrations

Code: `libs/storage/include/storage/schema_version.h`, `migrations.h`, `src/migrations.cpp`, `Database::Impl::ensureSchemaVersion` in `database.cpp`.
Tests: `tests/storage/test_schema_version.cpp`, `test_migrations.cpp`.

## Versions

- `kSchemaVersion` (9): the layout this build creates.
- `kMinSupportedVersion` (9): the oldest layout this build can open or upgrade from.
- v9 is the frozen baseline. v8 and lower were pre-release drafts and are refused with a "create a new project" message.
- The version lives in `PRAGMA user_version`. A new database is stamped with the target version when it is created (table count == 0).

## What happens when a project is opened

1. Below `kMinSupportedVersion`: refused (pre-release project).
2. Above the target version: refused ("created by a newer version of WHP").
3. Below the target version: `runMigrations(from)`:
   1. Check that a step exists for every version from `from + 1` to the target. If one is missing, throw before touching the file.
   2. Back up the project with `VACUUM INTO '<file>.v<from>.bak'` (skipped for `:memory:`).
   3. Run each step in its own transaction (see below).
4. Only then `createSchema()` runs (`CREATE ... IF NOT EXISTS` for tables and indexes). Migrating must come BEFORE it: `CREATE INDEX` on an old table that lacks the column fails.
5. If anything throws while opening, the SQLite handle is closed in the `Impl` constructor's catch block (a constructor that throws never runs its destructor, so the file would stay locked on Windows).

## One step

A step is `Migration{toVersion, needsForeignKeysOff, apply}`; `apply` receives a `SqlRunner` that runs SQL inside the step's transaction.

- The transaction contains the step's SQL AND `PRAGMA user_version = toVersion`. SQLite DDL and the version are transactional, so a failed step rolls back completely and the file stays at the old version (it can simply be retried).
- `needsForeignKeysOff = true` is for table rebuilds (SQLite cannot change constraints or column types in place; dropping a parent table would cascade-delete its children). `PRAGMA foreign_keys = OFF` is issued before the transaction, because it does nothing inside one. Before commit, `PRAGMA foreign_key_check` must return no rows or the step fails. Foreign keys are switched back on whether the step succeeds or fails.
- Errors are reported as "Upgrade to database vN failed: ... Your original project was backed up at <file>".

## The rule for every future schema change

Three edits, always together:
1. Edit `kSchemaSql` (new databases get the new layout).
2. Bump `kSchemaVersion`.
3. Add a step in `defaultMigrationPlan()` in `migrations.cpp` (old databases get upgraded).

Which kind of change is cheap or expensive:
- Cheap (additive): `ADD COLUMN`, new tables, new indexes.
- Expensive: changing constraints, column types, or keys. These are table rebuilds: create the new table, copy rows, drop the old one, rename, recreate indexes, with `needsForeignKeysOff = true`.

For the first real step (v10), also write an equivalence test: create a fresh database, upgrade a v9 fixture, and compare `sqlite_master` (tables, columns, indexes) between them.

## Test fixtures to add before real data

- A copy of a v9 project containing realistic data, kept in the repo's test data, so every future step is tested against a real old file.
- GEDCOM X export is the safety net: export before risky upgrades.

## Not built yet

- Steps that need C++ logic (for example computing `date_sort` for existing rows) cannot be written yet: `SqlRunner` only runs SQL. Extend the step context with a `query` function when the first such step is needed.
- A user-facing message in the app for the backup location (the text is already in the exception message that the log window shows).
