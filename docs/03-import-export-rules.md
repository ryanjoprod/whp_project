# WHP Program - Import / Export Rules (GEDCOM X)

## Import (one transaction; any failure rolls back everything)

Order inside `App` import:
1. places
2. sources (+ `ensureBareCitation` for each)
3. citations (`importCitations`)
4. people (`save(p, sourceId)`)
5. relationships (`save(r, sourceId)`)
6. agents
7. events (`save(e, sourceId)`)
8. documents (`save(d, sourceId)`)

Then `refreshKnownCitations()` and `knownSources_ = db_->loadAllSourceDescriptions()`.

Citation references are resolved first with `resolveCitationReferences(people, relationships, events, citations)` (a 3-argument overload forwards to the 4-argument one).

Rules still to implement:
- **Remap place ids** from the file to database ids, and apply the same map to `place_description_id` on facts and events. Unresolved id: clear it, keep the original text.
- **Remap agent ids** the same way (agents are global).
- A place parent cycle guard.
- Place parser/serializer for dated names, `code`, URLs and dated jurisdictions (stored but not yet read/written).

## Export

- `gatherSourceExport` builds a `SourceExport` (persons, relationships, source, other sources, citations, events). Row-key prefixes are stripped with `stripRowKeyPrefix`.
- `serializeSourceExport(source, persons, rels, otherSources, citations, events)`.
- Not yet exported: places, agents, documents. GEDCOM X export is the planned safety net for migrations.

## App extensions to GEDCOM X (our own JSON members)

- Date: `calendar`, `quality`, `sortValue`.
- Event: `description`.
- Source, citation, person: `attributes`.
- Source `citations` array: element 0 mirrors the legacy `sources.citation` text.

## Parser conventions (`parser.cpp`)

- Helpers live in the TOP-LEVEL anonymous namespace, not inside `namespace gedcomx`, so they must qualify types (`gedcomx::Date`, `gedcomx::Attribute`).
- Reading JSON: use `at()` / `contains()`; non-const `operator[]` inserts nulls.
- Key names are plural in files: `sourceDescriptions`, `persons`.
