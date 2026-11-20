# WHP Program - Dev Notes and Pitfalls

## The dominant bug class: SQL strings are not compiler-checked

Everything inside a SQL string compiles. Real bugs found so far, so check these first when a test says "SQL error":
- Table or column name typos (`source` vs `sources`, `person` vs `persons`, `date_origial`).
- Placeholder count not matching the column count, or binds in a different order than the column list.
- SELECT column order not matching the `columnText(s, n)` indexes. Use one shared column-list constant (`kDateColumns`, `kDocumentColumns`) for the SELECT.
- Missing spaces where string pieces are concatenated (`...FROM` glued to the previous word).
- `ON CONFLICT(x)` naming a column that is not the key any more.
- Typos in `excluded.` (`exluded`).
- SQLite does not validate `REFERENCES` target names at `CREATE TABLE` time; the error appears when a statement first touches the table.
- Forgetting `return` in a forwarder (C4716). Build with `/we4715 /we4716` (MSVC) or `-Werror=return-type` to make it an error.
- Forwarders on `Database::` need the `Database::` prefix or `impl_` is undefined; header and `.cpp` signatures must match.

## Quick schema check

Extract the raw string from `schema.h`, feed it to `sqlite3` line by line with `sqlite3.complete_statement`, and then check `PRAGMA foreign_key_list` for every table: referenced table exists, referenced column is a PK/unique, FK column exists. The last run was clean: 47 tables, no bad references, every table has a PK.

## doctest

- No `&&`, `||` or stray `=` inside `CHECK`; build expected sets first.
- Guard `REQUIRE(v.size() ...)` before indexing.
- Remember a test that saves a child must save its parent source/person first (FK failures).
- Helpers: `testutil::saveSource`, `saveSourceWithCitation`, `makeHebrewDate`, `checkHebrewDate`.

## ImGui

- Every `BeginTabItem` that returns true needs `EndTabItem`; every `PushID` needs `PopID` on all paths. "Missing PopID()" usually means one of those.
- Request/response between `Window` and `App`: the Window sets a pending request, `App` consumes it, queries the DB and pushes results back (`setX`).
- Owner enums so only the owning editor renders nested popups.
- Fixed-size tables inside `AlwaysAutoResize` popups (avoid `-FLT_MIN` widths).
- `InputText` buffers must be char arrays; structs must be declared before first use.

## Folder rules for this project

- Only `C:\Users\ryan\Documents\whp\whp_program` and `...\whp\test` are valid. The assistant uses them read-only unless told otherwise (the `docs/` folder was written with explicit permission). All data is fake; test databases are throwaway.
