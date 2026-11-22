# WHP Program - Design Decisions (and why)

Each entry: decision, then the reason. Revisit only with a migration plan.

1. **Dates inline, not a `dates` table.** A separate table adds orphan rows, joins and save-order problems for no gain. Cost: five columns repeated on dated tables.
2. **`date_sort` = JDN of the start date, NULL if unknown.** One sortable integer; ranges sort by start; quality never moves the sort.
3. **Keep both events and facts.** Facts for single-subject conclusions (person_facts, relationship_facts; marriage is a relationship fact). Events for multi-person happenings with roles. One UI list later; no `birth_ref_index`, pick by date.
4. **Events are source-scoped.** Same file-collision reason as persons. Roles must point at persons in the same source.
5. **Documents are source-scoped, agents global.** Documents belong to one source's file and nothing references them; agents (authors, repositories) are shared and remapped on import.
6. **Place ids from files are remapped on import**; `place_description_id` has no FK so unresolved ids can be cleared (original text kept).
7. **One person, one aggregate.** `person_links.person_id` is the PK; linking moves; merging folds a whole group.
8. **Row keys `sourceId:localId`** keep each imported file's local ids intact while the database stays globally unique.
9. **Cascade for owned children, RESTRICT for sources/citations.** Deleting a person cleans up everything they own; you cannot delete a source that is still cited.
10. **Additive-first schema evolution.** `ADD COLUMN`, new tables and indexes are cheap in migration; changing constraints or column types needs a table rebuild, so decide those before real data.
11. **v9 is the baseline.** v8 and lower were pre-release drafts whose layout changed while the schema was being built, so the version number was ambiguous. Opening a project below v9 is refused with a clear message ("create a new project"); no migration from them exists or is needed. Migration machinery starts at v9 -> v10. From now on any schema change bumps `kSchemaVersion` and ships with a migration step.
12. **Attribute-level citations and notes skipped.** Gramps' own SrcAttribute (source/citation) has none either; additive later.
13. **Place and agent ids are `<sourceId>:<fileId>`.** Deterministic, so there are no collisions across files and re-imports update instead of duplicating, with no mapping table. Duplicate real-world places are merged later by hand.
14. **Place cycles are refused in storage, regardless of dates.** Every route (import, future editor, hand-edited files) goes through `save`, and everything that walks the hierarchy can then assume it ends.

15. **One Date editor, built on `DateParts`.** Pure logic lives in `libs/gedcomx/date_parts.h` (`DateKind {Regular, Before, After, About, Range, Span, TextOnly}`, `SimpleDate{year, month, day}` with 0 = not given and negative year = BC, `DateParts{kind, calendar, quality, start, stop, text}`). The UI (`window_date_editor.cpp`) is a single "Date" modal opened from any date field with a `DateEditorTarget`; only the owning editor renders it, and it must be rendered inside the parent popup's scope. OK returns a complete `gedcomx::Date` (original, formal, calendar, quality, sortValue); Cancel changes nothing. Each new dated editor (fact, event, place) adds one target value and two calls.
16. **Formal date forms.** Regular `+1900-05-05`, About `A+1900`, Before `/+1900`, After `+1900/`, Range and Span `+X/+Y` (told apart on reload by the generated original text starting with `from `). Text-only has an empty formal. `formal` holds the numbers of the date's own calendar, so it is standard GEDCOM X only for Gregorian. A formal date the editor cannot parse reopens as Text only with its words and is only rewritten if the user presses OK.
17. **Only Gregorian and Julian compute `sortValue`.** Other calendars (Hebrew, French Republican, Persian, Islamic, Swedish) are stored but have no sort value until conversion is written. Text typed straight into a date box becomes a Text-only date with no sort value; the Date editor structures it. A free-text parser ("5 May 1900" -> Regular) is a possible later addition.

## Priority once the input popups are finished

- The main window's menu bar must always be available (check for modals blocking it; likely use `ImGui::BeginMainMenuBar()`).
- The main window must behave like a normal window when resized or snapped (for example, dragging it to the left edge fills the left half): resizable SDL window, layout and render target follow the window size, sensible minimum size.

## Out of scope for v1 (parked)

Gallery tab (after tree view and map view); export-all-sources (one file per source; check `person_links` in `gatherSourceExport` first); import that keeps existing values when the file is silent; export of places/agents/documents; non-Gregorian `date_sort` conversion.
