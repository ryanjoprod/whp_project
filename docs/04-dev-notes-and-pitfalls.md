# WHP Program - Dev Notes and Pitfalls

## The dominant bug class: SQL strings are not compiler-checked

Everything inside a SQL string compiles. Real bugs found so far, so check these first when a test says "SQL error":
- Table or column name typos (`source` vs `sources`, `person` vs `persons`, `date_origial`, `place_description` for `place_description_id`).
- Placeholder count not matching the column count, or binds in a different order than the column list.
- SELECT column order not matching the `columnText(s, n)` indexes. Use one shared column-list constant (`kDateColumns`, `kDocumentColumns`) for the SELECT.
- Missing spaces where string pieces are concatenated (`...FROM` glued to the previous word).
- An unclosed `(` in a subquery (`IN (SELECT ... WHERE x = ?` with no `)`) reports "near ;" at the END of the statement, far from the mistake.
- `ON CONFLICT(x)` naming a column that is not the key any more.
- Typos in `excluded.` (`exluded`).
- Wrong JSON key in a parser branch (`jurisdiction` vs `jurisdictions`): the branch silently reads nothing.
- SQLite does not validate `REFERENCES` target names at `CREATE TABLE` time; the error appears when a statement first touches the table.
- Forgetting `return` in a forwarder (C4716). Build with `/we4715 /we4716` (MSVC) or `-Werror=return-type` to make it an error.
- Forwarders on `Database::` need the `Database::` prefix or `impl_` is undefined; header and `.cpp` signatures must match.
- The SQL error message prints the statement: read it for the typo before anything else.

## C++ pitfalls seen in this project

- `for (auto x : items)` COPIES; if the loop modifies `x`, the change is lost. Use `auto&` (or `const auto&` to read without copying).
- A constructor that throws never runs its destructor: release resources (the SQLite handle) in a catch block inside the constructor.
- Put only DECLARATIONS in headers; bodies that use `Impl` belong in the `.cpp` (a constructor body pasted into the header gave C2535).
- A second copy of a declaration, or a comparison turned around (`<` for `>`), compiles fine and fails only in a test; every guard needs a test for both directions.
- Names inside `namespace storage { }` in a shared header must be inside that namespace too.
- `storage::stripRowKeyPrefix(rowKey, sourceId)` is `substr(sourceId.size() + 1)`: it chops blindly, so only call it on a real row key. For an id that may already be local, check that it starts with `<sourceId>:` first.
- `strncpy_s(dest, text, sizeof(wrongThing) - 1)`: the count must be the DESTINATION buffer's size; use `_TRUNCATE` so long text can never trip the CRT's invalid-parameter handler.
- Never build the object you save from the form alone: start from the loaded copy (`workingPlace_`, `workingEvent_`, the existing fact) and overwrite only what the form edits, or fields the form does not show are lost.

## Quick schema check

Extract the raw string from `schema.h`, feed it to `sqlite3` line by line with `sqlite3.complete_statement`, and then check `PRAGMA foreign_key_list` for every table: referenced table exists, referenced column is a PK/unique, FK column exists. The last run was clean: 47 tables, no bad references, every table has a PK.

## doctest

- No `&&`, `||` or stray `=` inside `CHECK`; build expected sets first.
- Guard `REQUIRE(v.size() ...)` before indexing.
- Remember a test that saves a child must save its parent source/person first (FK failures).
- Helpers: `testutil::saveSource`, `saveSourceWithCitation`, `makeHebrewDate`, `checkHebrewDate`.

## ImGui

- Every `BeginTabItem` that returns true needs `EndTabItem`; every `PushID` needs `PopID` on all paths. "Missing PopID()" usually means one of those.
- `BeginChild` always needs `EndChild`, even when it returns false.
- Request/response between `Window` and `App`: the Window sets a pending request, `App` consumes it, queries the DB and pushes results back (`setX`). Used for references, a person's events and a source's events.
- Owner enums so only the owning editor renders nested popups.
- Nested modals must be rendered INSIDE the parent popup's scope (Person editor -> Event Editor -> Citation Editor -> Date editor). When a button in one popup opens another, render the target BEFORE the opener in the frame (the Event Editor before the Add Existing Event picker) so the opener has closed by the time the target opens.
- Fixed-size tables inside `AlwaysAutoResize` popups (avoid `-FLT_MIN` widths); wrap each tab body in a fixed-size `BeginChild` so the popup does not resize per tab.
- `InputText` buffers must be char arrays; structs must be declared before first use.
- An open modal blocks input to every other ImGui window (see the main window note in the design decisions).

## Folder rules for this project

- Only `C:\Users\ryan\Documents\whp\whp_program` and `...\whp\test` are valid. The assistant uses them read-only unless told otherwise (the `docs/` folder was written with explicit permission). All data is fake; test databases are throwaway.
