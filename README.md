# WHP Program

A desktop genealogy visualizer written in modern C++. It reads and writes [GEDCOM X](https://github.com/FamilySearch/gedcomx) JSON, stores everything in a local SQLite database, and draws people and relationships as an interactive graph or a family tree.

WHP Program is also a learning project: a place to practice clean architecture, CMake, a database layer and immediate-mode graphics in one real application.

**Status:** version 0.1.0, first release. Windows (64-bit) is the supported platform. The database layout (schema v9) is frozen, and projects made with this version will be migrated forward by later versions.

## Features

- **Graph and tree views.** A force-directed graph, or a generational tree (ancestors above, descendants below, couples side by side). The choice is remembered per project.
- **Gramps-style editors** for people, relationships, events, places, sources and citations, including a date editor that handles ranges, "about", "before/after" and several calendars.
- **GEDCOM X import and export** per source, with the original files copied into the project.
- **Projects.** A project is a folder holding a `.whproj` settings file, a SQLite `.db` (the authoritative data) and a `sources/` folder. File -> Back Up Project makes a timestamped copy that is itself an openable project.
- **Large datasets.** Only a depth-limited neighborhood around a chosen person is shown at once.
- **Local only.** No account, no network access, no telemetry.

## Building

Requirements:

- Windows 10 or later, 64-bit
- Visual Studio with the "Desktop development with C++" workload (MSVC, CMake 3.28 or newer, Ninja and vcpkg are included)
- The `VCPKG_ROOT` environment variable pointing at a vcpkg install (Visual Studio's own developer prompts set it for you)

Dependencies (SDL3, Dear ImGui, SQLite, nlohmann-json, doctest) are declared in `vcpkg.json` and installed automatically on the first configure.

In Visual Studio, open the folder, choose the `windows-release` (or `windows-debug`) configuration, then Project -> Configure Cache and Build All.

From a terminal, use the **x64 Native Tools Command Prompt for VS**:

```
cmake --preset windows-release
cmake --build out/build/windows-release
```

The program is `out/build/windows-release/src/app/app.exe`.

### Tests

```
ctest --test-dir out/build/windows-release
```

### Packaging

```
cmake --install out/build/windows-release --prefix out/install
cd out/build/windows-release
cpack
```

This produces `WHP-Program-<version>-win64.zip`: the exe, its DLLs and the Visual C++ runtime. Unzip anywhere and run `app.exe`. More detail and the problems to watch for are in [docs/04-dev-notes-and-pitfalls.md](docs/04-dev-notes-and-pitfalls.md).

A `linux-debug` preset exists, but the project has not been built or tested on Linux yet. Only Windows is supported and packaged.

## Where files are stored

- Project data: in the project folder you pick (`.whproj`, `.db`, `sources/`, per-project physics settings).
- Per-user files (log, window position, ImGui layout): `%APPDATA%\WHP\WHP Program\`.

## Project layout

```
libs/gedcomx   GEDCOM X data model, JSON parsing/serialization, date and place logic, tree layout (no UI, no database)
libs/storage   SQLite persistence behind a pimpl-wrapped Database class, schema and migrations
src/app        The SDL3 + ImGui application: window, canvas, editors, project handling, logging
tests          doctest unit tests for all of the above
docs           Design notes (see below)
```

`gedcomx` has no dependency on `storage` or the UI, and `storage` hides SQLite from everything above it.

## Documentation

- [Schema overview](docs/01-schema-overview.md)
- [Design decisions and why](docs/02-design-decisions.md)
- [Import / export rules](docs/03-import-export-rules.md)
- [Dev notes and pitfalls](docs/04-dev-notes-and-pitfalls.md)
- [Schema versions and migrations](docs/05-migrations.md)

## Third-party software

| Library | License |
|---|---|
| [SDL3](https://libsdl.org) | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT |
| [SQLite](https://sqlite.org) | Public domain |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT |
| [doctest](https://github.com/doctest/doctest) (tests only) | MIT |

## License

No license has been chosen for WHP Program itself yet.
