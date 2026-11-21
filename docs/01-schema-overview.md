# WHP Program - Database Schema Overview (v9, frozen baseline)

Source of truth: `libs/storage/src/schema.h` (`kSchemaVersion = 9`, run through `exec(kSchemaSql)` in `database.cpp`).
`PRAGMA user_version` is set to `kSchemaVersion` when a database is created (table count == 0).
`PRAGMA foreign_keys = ON` is always set.

## Table groups (47 tables + indexes)

| Group | Tables |
|---|---|
| Sources | `sources`, `source_notes`, `source_attributes` |
| Citations | `citations`, `citation_notes`, `citation_attributes` |
| Persons | `persons`, `names`, `name_forms`, `name_parts`, `person_facts`, `person_fact_notes`, `person_fact_citations`, `person_fact_attributes`, `person_notes`, `person_citations`, `person_attributes`, `person_addresses`, `person_links` |
| Relationships | `relationships`, `relationship_facts`, `relationship_fact_notes`, `relationship_fact_citations`, `relationship_fact_attributes`, `relationship_notes`, `relationship_citations` |
| Events | `events`, `event_roles`, `event_notes`, `event_citations`, `event_attributes`, `event_role_notes`, `event_role_citations`, `event_role_attributes` |
| Places | `places`, `place_names`, `place_jurisdictions`, `place_urls`, `place_notes`, `place_citations` |
| Agents | `agents`, `agent_names`, `agent_emails`, `agent_phones`, `agent_addresses` |
| Documents | `documents`, `document_notes` |

## Id schemes

- **Row keys (`sourceId:localId`)**: persons, relationships, events, documents, and their owned children. The table has `row_key` (PK), `local_id`, `source_id`. `save()` takes LOCAL ids plus the source id; loads return ROW KEYS in `id` and fill `sourceId`. Exports strip prefixes with `storage::stripRowKeyPrefix`.
- **Global ids**: sources, citations, places, agents. Places and agents imported from a file are remapped to `<sourceId>:<fileId>` so files cannot collide (see import/export doc); sources and citations keep their own ids.
- Events and documents are source-scoped (`source_id NOT NULL`, FK to `sources` RESTRICT). Event roles reference persons by row key and must be in the same source; a dangling role fails the import and rolls back.

## Foreign key policy

- Owned children: `ON DELETE CASCADE`.
- References to sources and citations: `ON DELETE RESTRICT` (cannot delete something still cited).
- `place_jurisdictions.parent_id` is RESTRICT and DEFERRABLE INITIALLY DEFERRED.
- `place_description_id` on facts and events is deliberately UNCONSTRAINED (no FK) so an import can resolve place ids after the fact; unresolved ids are cleared and the original text is kept.
- `person_links`: `person_id` is the PRIMARY KEY (a person belongs to at most one aggregate); `aggregate_id` is a plain column. Linking an already linked person MOVES them (`ON CONFLICT(person_id) DO UPDATE`). `mergeAggregates(keep, absorb)` folds a whole group into another.

## Dates (inline on every dated row)

Columns: `date_original`, `date_formal`, `date_calendar`, `date_quality`, `date_sort INTEGER`.
- `""` in calendar/quality means Gregorian/none. `date_sort` NULL means unknown.
- **`date_sort` = Julian Day Number of the date's START**, NULL if unknown: month-only = 1st of the month, year-only = Jan 1, range/span = start, before/after/about = X. Quality does not change it.
- Gregorian JDN: `a=(14-m)/12; yy=y+4800-a; mm=m+12*a-3; JDN = d + (153*mm+2)/5 + 365*yy + yy/4 - yy/100 + yy/400 - 32045` (2000-01-01 = 2451545).
- Non-Gregorian conversion and the backfill of `date_sort` are not written yet. Dual dating and "new year begins" stay inside the original text.
- In SQL column lists, date columns go LAST (`kDateColumns`, `kDatePlaceholders`, `kDateUpdateSet`, `bindDate`, `readDate`).

## Places

`places(id, type, latitude REAL, longitude REAL, code, confidence, lang)`; dated `place_names`; hierarchy through `place_jurisdictions(place_id, parent_id, date...)`; `place_urls`. A place `title` is derived, not stored. Place and agent ids are `<sourceId>:<fileId>` when imported (generated ids when made in the app). A place can never be its own ancestor: `save(PlaceDescription)` refuses a cycle (see the import/export doc).

## Attributes

`Attribute{id, type, value}` tables exist for source, citation, person, person fact, event, event role, relationship fact. Code exists for source, citation and person; the others are tables only (adding the code changes no schema). Attribute-level citations and notes are intentionally skipped (additive later).

## Indexes

Every foreign key column that is not the leading column of its primary key has an index named `idx_<table>_<column>`, plus `idx_person_links_aggregate_id`. They are `CREATE INDEX IF NOT EXISTS` at the end of `kSchemaSql`. Adding indexes is additive; removing or changing them needs no data migration.

## Known leftovers

- `sources.citation` is the legacy single free-text citation. Kept: dropping it would lose data from imported files. Exported as `citations[0]`.
