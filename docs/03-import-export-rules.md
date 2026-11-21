# WHP Program - Import / Export Rules (GEDCOM X)

## Import (one transaction; any failure rolls back everything)

Before the transaction, after parsing:
1. `resolveCitationReferences(people, relationships, events, places, citations)` points references that name a real citation at that citation (persons, relationships, events, places and their facts/roles; three-, four- and five-argument forms exist, the shorter ones forward).
2. `remapPlaceAndAgentIds(sourceId, places, agents, people, relationships, events)` gives every place and agent the id `<sourceId>:<fileId>` and rewrites references. Call it ONCE per import (a second call would double-prefix).

Order inside the transaction:
1. sources (+ `ensureBareCitation` for each)
2. citations (`importCitations`)
3. places (AFTER citations: a place's `place_citations` rows have a foreign key to them)
4. people (`save(p, sourceId)`)
5. relationships (`save(r, sourceId)`)
6. agents
7. events (`save(e, sourceId)`)
8. documents (`save(d, sourceId)`)

Then `refreshKnownCitations()` and `knownSources_ = db_->loadAllSourceDescriptions()`.

## Place and agent ids

- Ids are deterministic and source-scoped: `PL1` from source `S1` is stored as `S1:PL1`. Two files can both say `PL1` without colliding, and re-importing a file updates its own rows instead of duplicating them (no mapping table needed).
- Cost: the same real-world place in two files is two rows until a future "merge places" feature joins them. A wrong automatic merge is worse than a duplicate.
- References rewritten: a place's parent places, and `place.descriptionId` on every person fact, relationship fact and event. A reference to a place the file does not define is cleared (the original text stays); a parent the file does not define is dropped.
- Places created in the app have generated ids with no prefix.
- Agents get the same prefix. Nothing references agents yet (`author` is plain text); when a reference is added (a source's repository, say) it must be remapped in `remapPlaceAndAgentIds`.

## Place hierarchy rules

- A place may never be its own ancestor. `save(PlaceDescription)` checks after writing the place's parents (recursive query over `place_jurisdictions`) and throws "would become its own ancestor"; a failed save undoes only that place, and inside an import the whole import rolls back. Cycles are refused even if the jurisdiction dates differ.
- Because the check runs on every save, a loop built across two saves in one transaction is caught on the save that closes it.
- Anything that walks up the hierarchy (derived place title, pickers) can rely on it being acyclic.

## Export

- `gatherSourceExport` builds a `SourceExport` (persons, relationships, source, other sources, citations, events, places). Row-key prefixes are stripped with `stripRowKeyPrefix`.
- Places in an export: those imported from the source (`<sourceId>:` prefix), those its facts and events use, and every ancestor of those. Ids of this source's places (and its facts' and events' place ids) are returned to the file's own ids (prefix stripped). A place from another source or created in the app keeps its full id and is exported under it.
- Places' own source references are exported with their citations (`noteRefs`).
- `serializeSourceExport(source, persons, rels, otherSources, citations, events, places)` writes a `places` array only when there are places.
- Not yet exported: agents, documents. GEDCOM X export is the planned safety net for migrations.

## App extensions to GEDCOM X (our own JSON members)

- Date: `calendar`, `quality`, `sortValue`.
- Event: `description`.
- Source, citation, person: `attributes`.
- Source `citations` array: element 0 mirrors the legacy `sources.citation` text.
- Place: `code`; `names[].date`; `urls[] {url, description, type}`; `jurisdictions[] {resource, date}`.
  - The standard `jurisdiction` member always holds the FIRST parent, so other programs still read the hierarchy. The richer `jurisdictions` list is written only when there is more than one parent or any has a date.
  - On reading, `jurisdictions` wins when present; otherwise the standard `jurisdiction` is used.

## Parser conventions (`parser.cpp`)

- Helpers live in the TOP-LEVEL anonymous namespace, not inside `namespace gedcomx`, so they must qualify types (`gedcomx::Date`, `gedcomx::Attribute`) when they name them; they can be CALLED unqualified from inside `namespace gedcomx`.
- Reading JSON: use `at()` / `contains()`; non-const `operator[]` inserts nulls. Check the right key (`jurisdictions` vs `jurisdiction`; `sourceDescriptions`, `persons` are plural).
- Loops that modify their elements must use `auto&` (a plain `auto` copies and the change is lost).

## Still to do

- Agent export; document export.
- A user-facing way to merge duplicate places.
- A packaged project (zip of `.whproj`, `.db`, `sources/`) for portability.
