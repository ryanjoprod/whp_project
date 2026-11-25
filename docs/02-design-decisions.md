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

15. **One Date editor, built on `DateParts`.** Pure logic lives in `libs/gedcomx/date_parts.h` (`DateKind {Regular, Before, After, About, Range, Span, TextOnly}`, `SimpleDate{year, month, day}` with 0 = not given and negative year = BC, `DateParts{kind, calendar, quality, start, stop, text}`). The UI (`window_date_editor.cpp`) is a single "Date" modal opened from any date field with a `DateEditorTarget`; only the owning editor renders it, and it must be rendered inside the parent popup's scope. OK returns a complete `gedcomx::Date` (original, formal, calendar, quality, sortValue); Cancel changes nothing. Each new dated editor (fact, event, place) adds one target value and two calls. `Window::renderDateInput(id, date, buffer, size, width)` is the shared text box plus `...` button.
16. **Formal date forms.** Regular `+1900-05-05`, About `A+1900`, Before `/+1900`, After `+1900/`, Range and Span `+X/+Y` (told apart on reload by the generated original text starting with `from `). Text-only has an empty formal. `formal` holds the numbers of the date's own calendar, so it is standard GEDCOM X only for Gregorian. A formal date the editor cannot parse reopens as Text only with its words and is only rewritten if the user presses OK.
17. **Only Gregorian and Julian compute `sortValue`.** Other calendars (Hebrew, French Republican, Persian, Islamic, Swedish) are stored but have no sort value until conversion is written. Text typed straight into a date box becomes a Text-only date with no sort value; the Date editor structures it. A free-text parser ("5 May 1900" -> Regular) is a possible later addition.

18. **Place Editor: one collector, and the working copy keeps what the form does not show.** `collectPlace()` builds a `PlaceDescription` from the buffers every frame (title readout, error line, OK enabled state, and what is saved). It starts from `workingPlace_`, so confidence, language and citations survive an edit. Rule for every editor: never build the saved object from the form alone, or fields the form does not show are lost.
19. **Place title is derived (`placeTitle`, `libs/gedcomx/place_logic.h`):** primary names up the FIRST parent chain, loop-safe. The parent picker excludes the place itself and its descendants (`placeDescendants`); the storage cycle guard (decision 14) is the backstop.
20. **Places are managed under Data -> Places...** (filterable list with New and Edit; `PlaceEditorOwner::App` or `List` decides which scope renders the editor). There is no delete yet: `place_description_id` has no FK, so deleting needs a "who uses this place" check first. Places carry citations through the same Citation Editor (`CitationEditorOwner::Place`); modals nest Place Editor -> Citation Editor -> Date editor.

21. **Facts and events are separate tabs.** In the Person editor, Facts = single-person conclusions (`person_facts`), Events = shared multi-person events. A merged list is still the long-term plan (decision 3). A fact stores a place as free text (`original`, what the source says) plus an optional link (`descriptionId`, picked with the shared place picker).
22. **Events are edited from a person and saved immediately.** Event roles have a foreign key to the person, so an event cannot be saved for an unsaved person; the Events tab says "save the person first". Shared records (events, citations) save at OK and not with the person, so cancelling the person editor does not undo them.
23. **Event Editor = the shared event + this person's role.** Opened from the Events tab (New event, Edit, Add existing event). It starts from the loaded event, so other people's roles, confidence and role notes survive. Other participants are read-only; each person joins an event from their own editor. One `EventChange` channel carries save, remove-from-event and delete; App deletes an event whose last role was removed. Loaded events carry row keys, new ones local ids; App converts to local ids only when the id really starts with `<sourceId>:` (`stripRowKeyPrefix` chops blindly). Modals nest Person editor -> Event Editor -> Citation Editor -> Date editor.

## Priority once the input popups are finished (the popups are now done)

Finding: the main window is created borderless (`SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE`) and its title bar is an ImGui window ("TitleBar", `window_titlebar.cpp`) with the menu, a maximize button and a close button. Dragging is done by hand (`SDL_GetGlobalMouseState`, `SDL_SetWindowPosition`). Consequences:
- An open modal popup blocks input to every other ImGui window, so the menu bar, the maximize/close buttons and the drag area stop working while any editor is open.
- The operating system does not know where the title bar or the resize borders are, so there is no native resize cursor, no snapping (drag to the left edge fills the left half) and no Windows snap layouts.

Plan (to be confirmed): `SDL_SetWindowHitTest` tells the OS which strip is the caption and which edges resize, so native drag, resize and snap work and the hand-written drag code goes away; the window buttons and menu bar get a way to stay usable while a popup is open (see the open question about which of these the menu bar should do during modals); a minimum window size; layout follows the window size.

## Out of scope for v1 (parked)

Gallery tab (after tree view and map view); export-all-sources (one file per source; check `person_links` in `gatherSourceExport` first); import that keeps existing values when the file is silent; export of places/agents/documents; non-Gregorian `date_sort` conversion; deleting places; adding other people to an event from inside the Event Editor (people join from their own editor today).
