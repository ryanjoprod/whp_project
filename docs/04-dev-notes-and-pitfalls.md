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
- `(flags && SDL_WINDOW_MAXIMIZED)` compiles and is true whenever ANY flag is set; a bit test needs `&`. A bitwise test mistyped as `&&` shows up only as "always true", so log the raw value when a flag check misbehaves.
- A log line placed after `return` never runs. When a debug line does not appear, check that it is reachable before suspecting the logger.
- Save and load paths must come from one helper (`userDataFolder()`); one side keeping a relative path means the file is written but never read.
- A one-character typo in logic (`unit == 1` for `unit == i`, `/` for `-`) compiles and gives plausible but wrong output; layout and ordering logic gets unit tests that check exact positions, not just "it ran".
- Pure logic that must be tested (`fitRectToDisplays`) lives in its own `.cpp` with no SDL, ImGui or logger includes, and is added to both the `app` and `whp_tests` targets.

## Quick schema check

Extract the raw string from `schema.h`, feed it to `sqlite3` line by line with `sqlite3.complete_statement`, and then check `PRAGMA foreign_key_list` for every table: referenced table exists, referenced column is a PK/unique, FK column exists. The last run was clean: 47 tables, no bad references, every table has a PK.

## doctest

- No `&&`, `||` or stray `=` inside `CHECK`; build expected sets first.
- Guard `REQUIRE(v.size() ...)` before indexing.
- Remember a test that saves a child must save its parent source/person first (FK failures).
- Helpers: `testutil::saveSource`, `saveSourceWithCitation`, `makeHebrewDate`, `checkHebrewDate`.

## ImGui

- Every `BeginTabItem` that returns true needs `EndTabItem`; every `PushID` needs `PopID` on all paths. "Missing PopID()" usually means one of those.
- `BeginMenu` returns true only while that menu is open, so a missing `EndMenu()` inside its braces runs fine until you hover the menu, then ImGui's assert aborts the debug build ("abort() has been called", nothing in the log). Same rule for every `if (BeginX(...))`: the matching `EndX()` goes inside the braces, after the items. A nested submenu needs its own `EndMenu()`.
- `BeginChild` always needs `EndChild`, even when it returns false.
- Request/response between `Window` and `App`: the Window sets a pending request, `App` consumes it, queries the DB and pushes results back (`setX`). Used for references, a person's events and a source's events.
- Owner enums so only the owning editor renders nested popups.
- Nested modals must be rendered INSIDE the parent popup's scope (Person editor -> Event Editor -> Citation Editor -> Date editor). When a button in one popup opens another, render the target BEFORE the opener in the frame (the Event Editor before the Add Existing Event picker) so the opener has closed by the time the target opens.
- Fixed-size tables inside `AlwaysAutoResize` popups (avoid `-FLT_MIN` widths); wrap each tab body in a fixed-size `BeginChild` so the popup does not resize per tab.
- `InputText` buffers must be char arrays; structs must be declared before first use.
- An open modal blocks input to every other ImGui window (see the main window note in the design decisions).
- ImGui remembers a window's position (popups are centered only on first use), so it never moves back when the main window shrinks. Clamp it yourself every frame (`Window::fitNextWindow`).
- The event-watch redraw is only for the OS resize drag. The main loop must call `App::renderFrame()` each iteration; if only the watcher draws, the UI lags by seconds and goes black when the watcher is off.
- Never call `SDL_RenderPresent` or `ImGui::NewFrame` from a window-event handler unless the redraw guard (`allowLiveRedraw_`) says the loop is between frames.

## Building a release

A release is the `windows-release` preset (Release, `/O2`, `NDEBUG`). Never ship anything from the Debug folder: it is slower and needs the Debug runtime.

Steps:
1. Configure and build. In Visual Studio pick `windows-release`, then Project -> Configure Cache and Build All. From a terminal use the **x64 Native Tools Command Prompt**: `cmake --preset windows-release`, then `cmake --build out/build/windows-release`.
2. Test the Release build: `ctest --test-dir out/build/windows-release` (one ctest entry runs every doctest case). Release can fail where Debug passes (uninitialized variables, undefined behavior).
3. Check the install layout: `cmake --install out/build/windows-release --prefix out/install`. It must hold `app.exe`, `SDL3.dll`, `sqlite3.dll` and the Visual C++ runtime DLLs (`vcruntime140*.dll`, `msvcp140*.dll`).
4. Package: run `cpack` inside `out/build/windows-release`. It writes `WHP-Program-<version>-win64.zip`.
5. Test the zip on a clean machine (Windows Sandbox is enough): it starts, opens a copy of a test project, File -> Back Up Project works, window state is remembered, and the log is at `%APPDATA%\WHP\WHP Program\whp_program.log`.
6. Version and tag: the version is `project(whp_program VERSION x.y.z)` in the root `CMakeLists.txt` (keep `vcpkg.json` in step). Commit, `git tag -a vX.Y.Z -m "..."`, then build the zip from the tagged commit.

Pitfalls found while making the first release:
- `add_compile_options` only affects targets created AFTER it. In the root `CMakeLists.txt` it must come before every `add_subdirectory`, or `/we4715 /we4716` silently does nothing. Also GCC and Clang flags are case-sensitive (`-Werror=return-type`).
- A configure from the wrong shell (not x64, or not `--preset`) builds a cache with a 32-bit `cl.exe` and no vcpkg toolchain, and the first `find_package` fails ("Could not find doctest"). Check `CMakeCache.txt` for `CMAKE_TOOLCHAIN_FILE`; if it is missing, delete the build folder and configure again.
- The `WIN32` (GUI subsystem) executable needs a `WinMain`. Include `<SDL3/SDL_main.h>` in `main.cpp` only, and write `int main(int argc, char* argv[])`. SDL renames `main` to `SDL_main`, so `int main()` becomes a different C++ function and fails with LNK2019 "unresolved external symbol SDL_main". With no console, `stdout` goes nowhere: the log file and the in-app log are the only diagnostics.
- A function that takes `std::filesystem::path` also accepts a string literal, silently. `Logger::init("whp_program.log")` kept compiling after the signature changed and kept writing to the current folder. After introducing a shared folder variable, search for leftover literals.
- Never call `.string()` on a path that can hold a user name (it goes through the ANSI code page). Pass `std::filesystem::path` through, and use `u8string()` plus `reinterpret_cast<const char*>` when a UTF-8 `std::string` is needed.
- Anything written at run time goes under `userDataFolder()` or the project folder, never relative to the current folder. After a change, start the app from another folder and check that no new files appear next to it.
- An editor autocomplete can change a CMake word (`set(CPACK_GENERATOR FindBZip2)` instead of `ZIP`); CPack then fails with "Could not create CPack generator". `CPackConfig.cmake` in the build folder shows what CPack was actually given.
- DLLs are installed with `install(FILES $<TARGET_RUNTIME_DLLS:app> DESTINATION .)`; the runtime with `InstallRequiredSystemLibraries`, after `CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION` is set to `.`. `include(CPack)` stays last in the root file.
- ImGui keeps the `IniFilename` pointer instead of copying the text. The path lives in a `Window` member (`imguiIniPath_`) so it outlives the ImGui context; `Window` must not be copied.

## Folder rules for this project

- Only `C:\Users\ryan\Documents\whp\whp_program` and `...\whp\test` are valid. The assistant uses them read-only unless told otherwise (the `docs/` folder was written with explicit permission). All data is fake; test databases are throwaway.
