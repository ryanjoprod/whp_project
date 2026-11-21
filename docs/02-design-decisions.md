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

## Out of scope for v1 (parked)

Gallery tab (after tree view and map view); export-all-sources (one file per source; check `person_links` in `gatherSourceExport` first); import that keeps existing values when the file is silent; export of places/agents/documents; non-Gregorian `date_sort` conversion.
