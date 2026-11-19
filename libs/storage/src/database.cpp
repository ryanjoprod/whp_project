#include "storage/database.h"
#include "schema.h"

#include <sqlite3.h>
#include <stdexcept>
#include <map>
#include <memory>
#include <set>
#include <algorithm>

namespace storage {
	class Database::Impl {
	public:
		explicit Impl(const std::string& path) {
			if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
				throw std::runtime_error("Failed to open database: " + path);
			}
			exec("PRAGMA foreign_keys = ON;");
			ensureSchemaVersion();
		}

		~Impl() {
			if (db_) {
				sqlite3_close(db_);
			}
		}

		void save(const gedcomx::Person& person, const std::string& sourceId) {
			std::string key = rowKey(sourceId, person.id);
			Transaction tx(*this);

			upsert("INSERT INTO persons (row_key, local_id, source_id, confidence, lang, gender) VALUES (?, ?, ?, ?, ?, ?) "
				"ON CONFLICT(row_key) "
				"DO UPDATE SET local_id = excluded.local_id, source_id = excluded.source_id, confidence = excluded.confidence, lang = excluded.lang, gender = excluded.gender;",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, key);
					bindText(s, 2, person.id);
					bindText(s, 3, sourceId);
					bindText(s, 4, person.conclusion.confidence);
					bindText(s, 5, person.conclusion.lang);
					bindText(s, 6, person.gender ? person.gender->type : "");
				});

			// Clear old names for this person before re-inseting (simple
			// replace-all strategy; fine until incremental updates matter).
			// The code below fixes leaving orphaned data
			exec("DELETE FROM names WHERE person_id = ?;", { key });

			for (const auto& name : person.names) {
				long long nameId = insertReturningId(
					"INSERT INTO names (person_id, title, nick_name, call_name) VALUES (?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, key);
						bindText(s, 2, name.title);
						bindText(s, 3, name.nickName);
						bindText(s, 4, name.callName);
					}
				);

				for (const auto& form : name.nameForms) {
					long long formId = insertReturningId(
						"INSERT INTO name_forms (name_id, full_text) VALUES (?, ?);",
						[&](sqlite3_stmt* s) {
							sqlite3_bind_int64(s, 1, nameId);
							bindText(s, 2, form.fullText);
						}
					);

					for (const auto& part : form.parts) {
						upsert("INSERT INTO name_parts (name_form_id, value, type) VALUES (?, ?, ?);",
							[&](sqlite3_stmt* s) {
								sqlite3_bind_int64(s, 1, formId);
								bindText(s, 2, part.value);
								bindText(s, 3, part.type);
							}
						);
					}
				}
			}

			// Cleans up orphaned data before saving
			exec("DELETE FROM person_facts WHERE person_id = ?;", { key });

			for (size_t i = 0; i < person.facts.size(); ++i) {
				const auto& fact = person.facts[i];
				const std::string localId = fact.id.empty() ? "f" + std::to_string(i) : fact.id;
				std::string factKey = rowKey(key, localId);
				upsert(std::string("INSERT INTO person_facts (row_key, local_id, person_id, type, place_original, place_description_id, value, confidence, lang, ") + kDateColumns + ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, " + kDatePlaceholders + ");",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, factKey);
						bindText(s, 2, localId);
						bindText(s, 3, key);
						bindText(s, 4, fact.type);
						bindText(s, 5, fact.place.original);
						bindText(s, 6, fact.place.descriptionId);
						bindText(s, 7, fact.value);
						bindText(s, 8, fact.conclusion.confidence);
						bindText(s, 9, fact.conclusion.lang);
						bindDate(s, 10, fact.date);
					});

				saveCitationRefs("person_fact_citations", "person_fact_id", factKey, fact.conclusion.sources);
				saveNotesFor("person_fact_notes", "person_fact_id", factKey, fact.conclusion.notes);
			}

			saveNotesFor("person_notes", "person_id", key, person.conclusion.notes);
			saveCitationRefs("person_citations", "person_id", key, person.conclusion.sources);
			saveAddressesFor(key, person.addresses);
			saveAttributesFor("person_attributes", "person_id", key, person.attributes);

			tx.commit();
		}

		std::vector<gedcomx::Person> loadAll() {
			std::map<std::string, gedcomx::Person> people;

			// One query per person is simple to reason about; a single join
			// would be faster but harder to read at this stage of the project.
			query("SELECT row_key, local_id, source_id, confidence, lang, gender FROM persons;", {},
				[&](sqlite3_stmt* s) {
					std::string key = columnText(s, 0);
					std::string localId = columnText(s, 1);

					gedcomx::Person person;
					person.id = key;
					person.sourceId = columnText(s, 2);
					person.names = loadNamesFor(key);
					person.facts = loadFactsForPerson(key);
					person.conclusion.confidence = columnText(s, 3);
					person.conclusion.lang = columnText(s, 4);
					person.conclusion.notes = loadNotesFor("person_notes", "person_id", key);
					person.conclusion.sources = loadCitationRefsFor("person_citations", "person_id", key);
					std::string genderType = columnText(s, 5);
					if (!genderType.empty()) person.gender = gedcomx::Gender{ genderType };
					person.attributes = loadAttributesFor("person_attributes", "person_id", key);
					person.addresses = loadAddressesFor(key);

					people[key] = std::move(person);
				});

			std::vector<gedcomx::Person> results;
			for (auto& [key, person] : people) results.push_back(std::move(person));
			
			return results;
		}

		void linkPersons(const std::string& aggregateId, const std::vector<PersonRef>& refs) {
			for (const auto& ref : refs) {
				std::string key = rowKey(ref.sourceId, ref.localId);

				upsert("INSERT INTO person_links (aggregate_id, person_id) VALUES (?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, aggregateId);
						bindText(s, 2, key);
					});
			}
		}

		std::vector<AggregatePerson> loadAggregatePersons() {
			// Gather the distinct aggregate ids first.
			std::vector<std::string> aggregateIds;

			query("SELECT DISTINCT aggregate_id FROM person_links;", {},
				[&](sqlite3_stmt* s) {
					aggregateIds.push_back(columnText(s, 0));
				});

			std::vector<AggregatePerson> results;
			for (const auto& aggId : aggregateIds) {
				AggregatePerson agg;
				agg.aggregateId = aggId;

				// Merge names/facts from every linked person into one combined record.
				// Naive concatenation for now - no deduplication of near-identical names.
				query("SELECT person_id FROM person_links WHERE aggregate_id = ?;", { aggId },
					[&](sqlite3_stmt* s) {
						std::string personKey = columnText(s, 0);
						auto names = loadNamesFor(personKey);
						auto facts = loadFactsForPerson(personKey);
						agg.names.insert(agg.names.end(), names.begin(), names.end());
						agg.facts.insert(agg.facts.end(), facts.begin(), facts.end());
					});

				results.push_back(std::move(agg));
			}

			return results;
		}

		std::optional<std::string> getAggregateIdForPerson(const std::string& personKey) {
			std::optional<std::string> result;
			query("SELECT aggregate_id FROM person_links WHERE person_id = ?;", { personKey },
				[&](sqlite3_stmt* s) {
					result = columnText(s, 0);
				});

			return result;
		}

		std::optional<gedcomx::Person> loadPerson(const std::string& personKey) {
			std::optional<gedcomx::Person> result;

			query("SELECT row_key, local_id, source_id, confidence, lang, gender FROM persons WHERE row_key = ?;", { personKey },
				[&](sqlite3_stmt* s) {
					std::string key = columnText(s, 0);
					std::string local_id = columnText(s, 1);

					gedcomx::Person person;
					person.id = key;
					person.sourceId = columnText(s, 2);
					person.names = loadNamesFor(key);
					person.facts = loadFactsForPerson(key);
					person.conclusion.confidence = columnText(s, 3);
					person.conclusion.lang = columnText(s, 4);
					person.conclusion.notes = loadNotesFor("person_notes", "person_id", key);
					person.conclusion.sources = loadCitationRefsFor("person_citations", "person_id", key);
					std::string genderType = columnText(s, 5);
					if (!genderType.empty()) person.gender = gedcomx::Gender{ genderType };
					person.attributes = loadAttributesFor("person_attributes", "person_id", key);
					person.addresses = loadAddressesFor(key);

					result = person;
				});

			return result;
		}

		std::vector<std::string> getLinkedPersonKeys(const std::string& personKey) {
			std::vector<std::string> result;

			query("SELECT DISTINCT pl2.person_id FROM person_links pl1 JOIN person_links pl2 ON pl1.aggregate_id = pl2.aggregate_id WHERE pl1.person_id = ? AND pl2.person_id != ?;", { personKey, personKey },
				[&](sqlite3_stmt* s) { result.push_back(columnText(s, 0)); });

			return result;
		}

		bool deletePerson(const std::string& personKey) {
			return deleteByKey("DELETE FROM persons WHERE row_key = ?;", personKey);
		}

		void saveRelationshipRow(const gedcomx::Relationship& rel, const std::string& sourceId) {
			if (rel.type.empty()) throw std::runtime_error("Relationship has no type: " + rel.id);

			std::string relKey = rowKey(sourceId, rel.id);
			std::string p1Key = rowKey(sourceId, rel.person1Id);
			std::string p2Key = rowKey(sourceId, rel.person2Id);

			upsert("INSERT INTO relationships (row_key, local_id, source_id, type, person1_id, person2_id, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
				"ON CONFLICT(row_key) "
				"DO UPDATE SET local_id = excluded.local_id, source_id = excluded.source_id, type = excluded.type, person1_id = excluded.person1_id, "
				"person2_id = excluded.person2_id, confidence = excluded.confidence, lang = excluded.lang;",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, relKey);
					bindText(s, 2, rel.id);
					bindText(s, 3, sourceId);
					bindText(s, 4, rel.type);
					bindText(s, 5, p1Key);
					bindText(s, 6, p2Key);
					bindText(s, 7, rel.conclusion.confidence);
					bindText(s, 8, rel.conclusion.lang);
				});

			saveCitationRefs("relationship_citations", "relationship_id", relKey, rel.conclusion.sources);

			exec("DELETE FROM relationship_facts WHERE relationship_id = ?;", { relKey });

			for (size_t i = 0; i < rel.facts.size(); ++i) {
				const auto& fact = rel.facts[i];
				const std::string localId = fact.id.empty() ? "f" + std::to_string(i) : fact.id;
				std::string factKey = rowKey(relKey, localId);
				upsert(std::string("INSERT INTO relationship_facts (row_key, local_id, relationship_id, type, place_original, place_description_id, value, confidence, lang, ") + kDateColumns + ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, " + kDatePlaceholders + ");",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, factKey);
						bindText(s, 2, localId);
						bindText(s, 3, relKey);
						bindText(s, 4, fact.type);
						bindText(s, 5, fact.place.original);
						bindText(s, 6, fact.place.descriptionId);
						bindText(s, 7, fact.value);
						bindText(s, 8, fact.conclusion.confidence);
						bindText(s, 9, fact.conclusion.lang);
						bindDate(s, 10, fact.date);
					});

				saveCitationRefs("relationship_fact_citations", "relationship_fact_id", factKey, fact.conclusion.sources);
				saveNotesFor("relationship_fact_notes", "relationship_fact_id", factKey, fact.conclusion.notes);
			}

			saveNotesFor("relationship_notes", "relationship_id", relKey, rel.conclusion.notes);
		}

		std::vector<gedcomx::Relationship> loadRelationshipsForPerson(const std::string& personKey) {
			std::vector<gedcomx::Relationship> rels;

			query("SELECT row_key, local_id, source_id, type, person1_id, person2_id, confidence, lang FROM relationships WHERE person1_id = ? OR person2_id = ? ORDER BY row_key;", { personKey, personKey },
				[&](sqlite3_stmt* s) {
					rels.push_back(loadRelationshipRow(s));
				});

			return rels;
		}

		std::vector<gedcomx::Relationship> loadChildLinks(const std::string& parentKey) {
			std::vector<gedcomx::Relationship> rels;

			query("SELECT row_key, local_id, source_id, type, person1_id, person2_id, confidence, lang "
				"FROM relationships WHERE person1_id = ? AND type = ? ORDER BY row_key;",
				{ parentKey, "http://gedcomx.org/ParentChild" },
				[&](sqlite3_stmt* s) {
					rels.push_back(loadRelationshipRow(s));
				});

			return rels;
		}

		void save(const gedcomx::Relationship& rel, const std::string& sourceId) {
			Transaction tx(*this);
			saveRelationshipRow(rel, sourceId);
			tx.commit();
		}

		void saveAll(const std::vector<gedcomx::Relationship>& relationships, const std::string& sourceId) {
			Transaction tx(*this);
			for (const auto& rel : relationships) {
				saveRelationshipRow(rel, sourceId);
			}
			tx.commit();
		}

		bool deleteRelationship(const std::string& relationshipKey) {
			return deleteByKey("DELETE FROM relationships WHERE row_key = ?;", relationshipKey);
		}

		void save(const gedcomx::SourceDescription& source) {
			Transaction tx(*this);
			upsert("INSERT INTO sources (id, title, citation, author, publication, abbreviation) "
				"VALUES (?, ?, ?, ?, ?, ?) "
				"ON CONFLICT(id) "
				"DO UPDATE SET title = excluded.title, citation = excluded.citation, author = excluded.author, publication = excluded.publication, abbreviation = excluded.abbreviation;",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, source.id);
					bindText(s, 2, source.primaryTitle());
					bindText(s, 3, source.citations.empty() ? "" : source.citations[0].value);
					bindText(s, 4, source.author);
					bindText(s, 5, source.publication);
					bindText(s, 6, source.abbreviation);
				});
			saveNotesFor("source_notes", "source_id", source.id, source.notes);
			saveAttributesFor("source_attributes", "source_id", source.id, source.attributes);
			tx.commit();
		}

		std::vector<gedcomx::SourceDescription> loadAllSourceDescriptions() {
			std::vector<gedcomx::SourceDescription> results;

			query("SELECT id, title, citation, author, publication, abbreviation FROM sources;", {},
				[&](sqlite3_stmt* s) {
					gedcomx::SourceDescription src;
					src.id = columnText(s, 0);
					src.titles.push_back({ "", columnText(s, 1) });
					src.citations.push_back({ "", columnText(s, 2) });
					src.author = columnText(s, 3);
					src.publication = columnText(s, 4);
					src.abbreviation = columnText(s, 5);
					src.notes = loadNotesFor("source_notes", "source_id", src.id);
					src.attributes = loadAttributesFor("source_attributes", "source_id", src.id);
					results.push_back(std::move(src));
				});

			return results;
		}

		void save(const gedcomx::Citation& c) {
			Transaction tx(*this);
			upsert(std::string("INSERT INTO citations (id, source_id, page, confidence, ") + kDateColumns + ") VALUES (?, ?, ?, ?, " + kDatePlaceholders + ")"
				"ON CONFLICT(id) "
				"DO UPDATE SET source_id = excluded.source_id, page = excluded.page, confidence = excluded.confidence, " + kDateUpdateSet,
				[&](sqlite3_stmt* s) {
					bindText(s, 1, c.id);
					bindText(s, 2, c.sourceId);
					bindText(s, 3, c.page);
					bindText(s, 4, c.confidence);
					bindDate(s, 5, c.date);
				});
			saveNotesFor("citation_notes", "citation_id", c.id, c.notes);
			saveAttributesFor("citation_attributes", "citation_id", c.id, c.attributes);
			tx.commit();
		}

		gedcomx::Citation loadCitationRow(sqlite3_stmt* s) {
			gedcomx::Citation c;
			c.id = columnText(s, 0);
			c.sourceId = columnText(s, 1);
			c.page = columnText(s, 2);
			c.confidence = columnText(s, 3);
			c.date = readDate(s, 4);
			c.notes = loadNotesFor("citation_notes", "citation_id", c.id);
			c.attributes = loadAttributesFor("citation_attributes", "citation_id", c.id);
			return c;
		}

		std::optional<gedcomx::Citation> loadCitation(const std::string& id) {
			std::optional<gedcomx::Citation> result;
			query(std::string("SELECT id, source_id, page, confidence, ") + kDateColumns + " FROM citations WHERE id = ? ; ",
				{ id },
				[&](sqlite3_stmt* s) {
					result = loadCitationRow(s);
				});
			return result;
		}

		std::vector<gedcomx::Citation> loadCitationsForSource(const std::string& sourceId) {
			std::vector<gedcomx::Citation> results;
			query(std::string("SELECT id, source_id, page, confidence, ") + kDateColumns + " FROM citations WHERE source_id = ? ORDER BY id; ",
				{ sourceId },
				[&](sqlite3_stmt* s) {
					results.push_back(loadCitationRow(s));
				});
			return results;
		}

		std::vector<gedcomx::Citation> loadAllCitations() {
			std::vector<gedcomx::Citation> results;
			query(std::string("SELECT id, source_id, page, confidence, ") + kDateColumns + " FROM citations ORDER BY source_id, id; ",
				{},
				[&](sqlite3_stmt* s) {
					results.push_back(loadCitationRow(s));
				});
			return results;
		}

		std::string ensureBareCitation(const std::string& sourceId) {
			const std::string id = gedcomx::bareCitationId(sourceId);
			upsert("INSERT INTO citations (id, source_id) VALUES (?, ?) ON CONFLICT(id) DO NOTHING;",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, id);
					bindText(s, 2, sourceId);
				});
			return id;
		}

		void save(const gedcomx::Agent& agent) {
			upsert("INSERT OR IGNORE INTO agents (id) VALUES (?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, agent.id);
				});

			exec("DELETE FROM agent_names WHERE agent_id = ?;", { agent.id });
			for (const auto& name : agent.names) {
				upsert("INSERT INTO agent_names (agent_id, lang, value) VALUES (?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, agent.id);
						bindText(s, 2, name.lang);
						bindText(s, 3, name.value);
					});
			}

			exec("DELETE FROM agent_emails WHERE agent_id = ?;", { agent.id });
			for (const auto& email : agent.emails) {
				upsert("INSERT INTO agent_emails (agent_id, email) VALUES (?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, agent.id);
						bindText(s, 2, email);
					});
			}

			exec("DELETE FROM agent_phones WHERE agent_id = ?;", { agent.id });
			for (const auto& phone : agent.phones) {
				upsert("INSERT INTO agent_phones (agent_id, phone) VALUES (?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, agent.id);
						bindText(s, 2, phone);
					});
			}		   

			exec("DELETE FROM agent_addresses WHERE agent_id = ?;", { agent.id });
			for (const auto& addr : agent.addresses) {
				upsert("INSERT INTO agent_addresses (agent_id, street, city, state_or_province, postal_code, country) VALUES (?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, agent.id);
						bindText(s, 2, addr.street);
						bindText(s, 3, addr.city);
						bindText(s, 4, addr.stateOrProvince);
						bindText(s, 5, addr.postalCode);
						bindText(s, 6, addr.country);
					});
			}
		}

		std::vector<gedcomx::Agent> loadAllAgents() {
			std::vector<gedcomx::Agent> results;

			query("SELECT id FROM agents;", {},
				[&](sqlite3_stmt* s) {
					gedcomx::Agent agent;
					agent.id = columnText(s, 0);

					query("SELECT lang, value FROM agent_names WHERE agent_id = ?;", { agent.id },
						[&](sqlite3_stmt* ns) {
							agent.names.push_back({ columnText(ns, 0), columnText(ns ,1) });
						});

					query("SELECT email FROM agent_emails WHERE agent_id = ?;", { agent.id },
						[&](sqlite3_stmt* es) {
							agent.emails.push_back(columnText(es, 0));
						});

					query("SELECT phone FROM agent_phones WHERE agent_id = ?;", { agent.id },
						[&](sqlite3_stmt* ps) {
							agent.phones.push_back(columnText(ps, 0));
						});

					query("SELECT street, city, state_or_province, postal_code, country FROM agent_addresses WHERE agent_id = ?;", { agent.id },
						[&](sqlite3_stmt* as) {
							gedcomx::Address addr;
							addr.street = columnText(as, 0);
							addr.city = columnText(as, 1);
							addr.stateOrProvince = columnText(as, 2);
							addr.postalCode = columnText(as, 3);
							addr.country = columnText(as, 4);

							agent.addresses.push_back(std::move(addr));
						});

					results.push_back(std::move(agent));
				});

				return results;
		}

		void save(const gedcomx::Event& event, const std::string& sourceId) {
			Transaction tx(*this);
			const std::string key = rowKey(sourceId, event.id);

			upsert(std::string("INSERT INTO events (row_key, local_id, source_id, type, place_original, place_description_id, description, confidence, lang, ") + kDateColumns + ") "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, " + kDatePlaceholders + ") "
				"ON CONFLICT(row_key) "
				"DO UPDATE SET local_id = excluded.local_id, source_id = excluded.source_id, type = excluded.type, place_original = excluded.place_original, place_description_id = excluded.place_description_id, description = excluded.description, confidence = excluded.confidence, lang = excluded.lang, " + kDateUpdateSet + ";",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, key);
					bindText(s, 2, event.id);
					bindText(s, 3, sourceId);
					bindText(s, 4, event.type);
					bindText(s, 5, event.place.original);
					bindText(s, 6, event.place.descriptionId);
					bindText(s, 7, event.description);
					bindText(s, 8, event.conclusion.confidence);
					bindText(s, 9, event.conclusion.lang);
					bindDate(s, 10, event.date);
				});

			exec("DELETE FROM event_roles WHERE event_id = ?;", { key });  // cascades to role notes and citations
			for (const auto& role : event.roles) {
				long long roleRowId = insertReturningId(
					"INSERT INTO event_roles (event_id, type, person_id, details, confidence, lang) VALUES (?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, key);
						bindText(s, 2, role.type);
						bindText(s, 3, rowKey(sourceId, role.personId));
						bindText(s, 4, role.details);
						bindText(s, 5, role.conclusion.confidence);
						bindText(s, 6, role.conclusion.lang);
					});

				for (const auto& note : role.conclusion.notes) {
					upsert("INSERT INTO event_role_notes (event_role_id, lang, subject, text) VALUES (?, ?, ?, ?);",
						[&](sqlite3_stmt* s) {
							sqlite3_bind_int64(s, 1, roleRowId);
							bindText(s, 2, note.lang);
							bindText(s, 3, note.subject);
							bindText(s, 4, note.text);
						});
				}

				saveCitationRefs("event_role_citations", "event_role_id", std::to_string(roleRowId), role.conclusion.sources);
			}

			saveNotesFor("event_notes", "event_id", key, event.conclusion.notes);
			saveCitationRefs("event_citations", "event_id", key, event.conclusion.sources);
			tx.commit();
		}

		gedcomx::Event loadEventRow(sqlite3_stmt* s) {
			gedcomx::Event event;
			const std::string key = columnText(s, 0);
			event.id = key;  // the row key, like Person
			event.sourceId = columnText(s, 2);
			event.type = columnText(s, 3);
			event.place.original = columnText(s, 4);
			event.place.descriptionId = columnText(s, 5);
			event.description = columnText(s, 6);
			event.conclusion.confidence = columnText(s, 7);
			event.conclusion.lang = columnText(s, 8);
			event.date = readDate(s, 9);
			event.conclusion.notes = loadNotesFor("event_notes", "event_id", key);
			event.conclusion.sources = loadCitationRefsFor("event_citations", "event_id", key);

			query("SELECT id, type, person_id, details, confidence, lang FROM event_roles WHERE event_id = ? ORDER BY id;", { key },
				[&](sqlite3_stmt* rs) {
					gedcomx::EventRole role;
					long long roleRowId = sqlite3_column_int64(rs, 0);
					role.type = columnText(rs, 1);
					role.personId = columnText(rs, 2);  // the person's row key
					role.details = columnText(rs, 3);
					role.conclusion.confidence = columnText(rs, 4);
					role.conclusion.lang = columnText(rs, 5);
					role.conclusion.notes = loadNotesFor("event_role_notes", "event_role_id", std::to_string(roleRowId));
					role.conclusion.sources = loadCitationRefsFor("event_role_citations", "event_role_id", std::to_string(roleRowId));
					event.roles.push_back(std::move(role));
				});
			return event;
		}

		std::vector<gedcomx::Event> loadAllEvents() {
			std::vector<gedcomx::Event>	results;
			query(std::string("SELECT row_key, local_id, source_id, type, place_original, place_description_id, description, confidence, lang, ") + kDateColumns + " FROM events ORDER BY row_key;",
				{},
				[&](sqlite3_stmt* s) {
					results.push_back(loadEventRow(s));
				});
			return results;
		}

		std::vector<gedcomx::Event> loadEventsForSource(const std::string& sourceId) {
			std::vector<gedcomx::Event> results;
			query(std::string("SELECT row_key, local_id, source_id, type, place_original, place_description_id, description, confidence, lang, ") + kDateColumns + " FROM events WHERE source_id = ? ORDER BY row_key;",
				{ sourceId },
				[&](sqlite3_stmt* s) {
					results.push_back(loadEventRow(s));
				});
			return results;
		}

		void save(const gedcomx::Document& doc) {
			upsert("INSERT INTO documents (id, type, text_type, extracted, text, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?) "
				"ON CONFLICT(id) "
				"DO UPDATE SET type = excluded.type, text_type = excluded.text_type, extracted = excluded.extracted, "
				"text = exluded.text, confidence = excluded.confidence, lang = excluded.lang;",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, doc.id);
					bindText(s, 2, doc.type);
					bindText(s, 3, doc.textType);
					sqlite3_bind_int(s, 4, doc.extracted ? 1 : 0);
					bindText(s, 5, doc.text);
					bindText(s, 6, doc.conclusion.confidence);
					bindText(s, 7, doc.conclusion.lang);
				});

			saveNotesFor("document_notes", "document_id", doc.id, doc.conclusion.notes);
		}

		std::vector<gedcomx::Document> loadAllDocuments() {
			std::vector<gedcomx::Document> results;

			query("SELECT id, type, text_type, extracted, text, confidence, lang FROM documents;", {},
				[&](sqlite3_stmt* s) {
					gedcomx::Document doc;

					doc.id = columnText(s, 0);
					doc.type = columnText(s, 1);
					doc.textType = columnText(s, 2);
					doc.extracted = sqlite3_column_int(s, 3) != 0;
					doc.text = columnText(s, 4);
					doc.conclusion.confidence = columnText(s, 5);
					doc.conclusion.lang = columnText(s, 6);
					doc.conclusion.notes = loadNotesFor("document_notes", "document_id", doc.id);

					results.push_back(std::move(doc));
				});

			return results;
		}
	
		void save(const gedcomx::PlaceDescription& place) {
			Transaction tx(*this);

			upsert("INSERT INTO places (id, type, latitude, longitude, code, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?) "
				"ON CONFLICT(id) "
				"DO UPDATE SET type = excluded.type, latitude = excluded.latitude, longitude = excluded.longitude, "
				"code = excluded.code, confidence = excluded.confidence, lang = excluded.lang;",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, place.id);
					bindText(s, 2, place.type);
					if (place.latitude) sqlite3_bind_double(s, 3, *place.latitude);
					else sqlite3_bind_null(s, 3);
					if (place.longitude) sqlite3_bind_double(s, 4, *place.longitude);
					else sqlite3_bind_null(s, 4);
					bindText(s, 5, place.code);
					bindText(s, 6, place.conclusion.confidence);
					bindText(s, 7, place.conclusion.lang);
				});

			exec("DELETE FROM place_names WHERE place_id = ?;", { place.id });
			for (const auto& name : place.names) {
				upsert(std::string("INSERT INTO place_names (place_id, lang, value, ") + kDateColumns + ") VALUES (?, ?, ?, " + kDatePlaceholders + ");",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, place.id);
						bindText(s, 2, name.lang);
						bindText(s, 3, name.value);
						bindDate(s, 4, name.date);
					});
			}

			exec("DELETE FROM place_jurisdictions WHERE place_id = ?;", { place.id });
			for (const auto& j : place.jurisdictions) {
				upsert(std::string("INSERT INTO place_jurisdictions (place_id, parent_id, ") + kDateColumns + ") VALUES (?, ?, " + kDatePlaceholders + ");",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, place.id);
						bindText(s, 2, j.parentId);
						bindDate(s, 3, j.date);
					});
			}

			exec("DELETE FROM place_urls WHERE place_id = ?;", { place.id });
			for (const auto& u : place.urls) {
				upsert("INSERT INTO place_urls (place_id, url, description, type) VALUES (?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, place.id);
						bindText(s, 2, u.url);
						bindText(s, 3, u.description);
						bindText(s, 4, u.type);
					});
			}

			saveNotesFor("place_notes", "place_id", place.id, place.conclusion.notes);
			saveCitationRefs("place_citations", "place_id", place.id, place.conclusion.sources);

			tx.commit();
		 }

		std::vector<gedcomx::PlaceDescription> loadAllPlaces() {
			std::vector<gedcomx::PlaceDescription> results;

			query("SELECT id, type, latitude, longitude, code, confidence, lang FROM places;", {},
				[&](sqlite3_stmt* s) {
					gedcomx::PlaceDescription place;

					place.id = columnText(s, 0);
					place.type = columnText(s, 1);

					if (sqlite3_column_type(s, 2) != SQLITE_NULL) place.latitude = sqlite3_column_double(s, 2);
					if (sqlite3_column_type(s, 3) != SQLITE_NULL) place.longitude = sqlite3_column_double(s, 3);

					place.code = columnText(s, 4);
					place.conclusion.confidence = columnText(s, 5);
					place.conclusion.lang = columnText(s, 6);
					place.conclusion.notes = loadNotesFor("place_notes", "place_id", place.id);
					place.conclusion.sources = loadCitationRefsFor("place_citations", "place_id", place.id);

					query(std::string("SELECT lang, value, ") + kDateColumns + " FROM place_names WHERE place_id = ? ORDER BY id;", { place.id },
						[&](sqlite3_stmt* ns) {
							place.names.push_back({ columnText(ns, 0), columnText(ns, 1), readDate(ns, 2) });
						});

					query(std::string("SELECT parent_id, ") + kDateColumns + " FROM place_jurisdictions WHERE place_id = ? ORDER BY id;", { place.id },
						[&](sqlite3_stmt* js) {
							place.jurisdictions.push_back({ columnText(js, 0), readDate(js, 1) });
						});

					query("SELECT url, description, type FROM place_urls WHERE place_id = ? ORDER BY id;", { place.id },
						[&](sqlite3_stmt* us) {
							place.urls.push_back({ columnText(us, 0), columnText(us, 1), columnText(us, 2) });
						});

					results.push_back(std::move(place));
				});

			return results;
		}

		std::vector<gedcomx::Person> searchPersonsByName(const std::string& query, const std::string& sourceIdFilter, int limit) {
			std::vector<gedcomx::Person> results;
			std::vector<std::string> matchedKeys;

			// Find distinct person row_keys whose full_text matches, via the existing
			// names -> name_forms chain, capped before doing the more expensive
			// full loadPerson() reconstruction for each hit.
			std::string sql =
				"SELECT DISTINCT persons.row_key FROM persons "
				"JOIN names ON names.person_id = persons.row_key "
				"JOIN name_forms ON name_forms.name_id = names.id "
				"WHERE name_forms.full_text LIKE ?";
			if (!sourceIdFilter.empty()) sql += " AND persons.source_id = ?";
			sql += " LIMIT ?;";

			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare search query: " + std::string(sqlite3_errmsg(db_)));

			std::string pattern = "%" + query + "%";
			int idx = 1;
			sqlite3_bind_text(stmt, idx++, pattern.c_str(), -1, SQLITE_TRANSIENT);
			if (!sourceIdFilter.empty()) sqlite3_bind_text(stmt, idx++, sourceIdFilter.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, idx++, limit);

			while (sqlite3_step(stmt) == SQLITE_ROW) {
				matchedKeys.push_back(columnText(stmt, 0));
			}
			sqlite3_finalize(stmt);

			for (const auto& key : matchedKeys) {
				auto person = loadPerson(key);
				if (person) results.push_back(std::move(*person));
			}

			return results;
		}

		SourceExport gatherSourceExport(const std::string& sourceId) {
			std::set<std::string> citationIds;
			std::set<std::string> otherSourceIds;
			SourceExport result;

			query("SELECT row_key FROM persons WHERE source_id = ?;", { sourceId },
				[&](sqlite3_stmt* s) {
					auto person = loadPerson(columnText(s, 0));
					if (person) {
						person->id = stripRowKeyPrefix(person->id, sourceId);
						result.persons.push_back(std::move(*person));
					}
			});

			query("SELECT row_key, local_id, source_id, type, person1_id, person2_id, confidence, lang FROM  relationships WHERE source_id = ?;", { sourceId },
				[&](sqlite3_stmt* s) {
					gedcomx::Relationship rel = loadRelationshipRow(s);
					rel.person1Id = stripRowKeyPrefix(rel.person1Id, sourceId);
					rel.person2Id = stripRowKeyPrefix(rel.person2Id, sourceId);
					result.relationships.push_back(std::move(rel));
			});

			for (auto& e : loadEventsForSource(sourceId)) {
				e.id = stripRowKeyPrefix(e.id, sourceId);
				for (auto& r : e.roles)
					r.personId = stripRowKeyPrefix(r.personId, sourceId);
				result.events.push_back(std::move(e));
			}

			for (auto& c : loadCitationsForSource(sourceId)) {
				if (gedcomx::isBare(c)) continue;
				citationIds.insert(c.id);
				result.citations.push_back(std::move(c));
			}

			auto noteRefs = [&](const std::vector<gedcomx::SourceReference>& refs) {
				for (const auto& ref : refs) {
					if (auto bareSource = gedcomx::bareCitationSourceId(ref.citationId)) {
						if (*bareSource != sourceId) otherSourceIds.insert(*bareSource);
					}
					else if (citationIds.insert(ref.citationId).second) {  // not exported yet
						if (auto c = loadCitation(ref.citationId)) {
							if (c->sourceId != sourceId) otherSourceIds.insert(c->sourceId);
							result.citations.push_back(std::move(*c));
						}
					}
				}
			};

			for (const auto& p : result.persons) {
				noteRefs(p.conclusion.sources);
				for (const auto& f : p.facts) noteRefs(f.conclusion.sources);
			}

			for (const auto& r : result.relationships) {
				noteRefs(r.conclusion.sources);
				for (const auto& f : r.facts) noteRefs(f.conclusion.sources);
			}

			for (const auto& e : result.events) {
				noteRefs(e.conclusion.sources);
				for (const auto& r : e.roles)
					noteRefs(r.conclusion.sources);
			}

			if (!otherSourceIds.empty()) {
				for (auto& s : loadAllSourceDescriptions()) {
					if (otherSourceIds.count(s.id)) result.otherSources.push_back(std::move(s));
				}
			}

			return result;
		}

		void saveAttributesFor(const std::string& table, const std::string& fkColumn, const std::string& ownerKey, const std::vector<gedcomx::Attribute>& attrs) {
			exec("DELETE FROM " + table + " WHERE " + fkColumn + " = ?; ", { ownerKey });
			for (size_t i = 0; i < attrs.size(); ++i) {
				const auto& a = attrs[i];
				const std::string attrKey = rowKey(ownerKey, a.id.empty() ? "a" + std::to_string(i) : a.id);
				upsert("INSERT INTO " + table + " (row_key, " + fkColumn + ", type, value) VALUES(?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, attrKey);
						bindText(s, 2, ownerKey);
						bindText(s, 3, a.type);
						bindText(s, 4, a.value);
					});
			}
		}

		std::vector<gedcomx::Attribute> loadAttributesFor(const std::string& table, const std::string& fkColumn, const std::string& ownerKey) {
			std::vector<gedcomx::Attribute> result;
			query("SELECT row_key, type, value FROM " + table + " WHERE " + fkColumn + " = ?;", { ownerKey },
				[&](sqlite3_stmt* s) {
					gedcomx::Attribute a;
					a.id = stripSourcePrefix(columnText(s, 0), ownerKey);
					a.type = columnText(s, 1);
					a.value = columnText(s, 2);
					result.push_back(std::move(a));
				});

			return result;
		}

		void saveAddressesFor(const std::string& ownerKey, const std::vector<gedcomx::Address>& addrs) {
			exec("DELETE FROM person_addresses WHERE person_id = ?;", { ownerKey });
			for (const auto& addr : addrs) {
				std::string addrKey = rowKey(ownerKey, addr.id);
				upsert("INSERT INTO person_addresses (row_key, person_id, street, city, state_or_province, postal_code, country) VALUES (?, ?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, addrKey);
						bindText(s, 2, ownerKey);
						bindText(s, 3, addr.street);
						bindText(s, 4, addr.city);
						bindText(s, 5, addr.stateOrProvince);
						bindText(s, 6, addr.postalCode);
						bindText(s, 7, addr.country);
					});
			}
		}

		std::vector<gedcomx::Address> loadAddressesFor(const std::string& ownerKey) {
			std::vector<gedcomx::Address> result;
			query("SELECT row_key, street, city, state_or_province, postal_code, country FROM person_addresses WHERE person_id = ?;", { ownerKey },
				[&](sqlite3_stmt* s) {
					gedcomx::Address addr;
					addr.id = stripRowKeyPrefix(columnText(s, 0), ownerKey);
					addr.street = columnText(s, 1);
					addr.city = columnText(s, 2);
					addr.stateOrProvince = columnText(s, 3);
					addr.postalCode = columnText(s, 4);
					addr.country = columnText(s, 5);
					result.push_back(std::move(addr));
				});

			return result;
		}

		void runInTransaction(const std::function<void()>& work) {
			Transaction tx(*this);
			work();  // any throw unwinds, so the guard rolls back
			tx.commit();
		}

		std::string personLabel(const std::string& personKey) {
			std::string label;
			query("SELECT name_forms.full_text FROM names JOIN name_forms ON name_forms.name_id = names.id "
				"WHERE names.person_id = ? ORDER BY names.id, name_forms.id LIMIT 1;", { personKey },
				[&](sqlite3_stmt* s) { label = columnText(s, 0); });
			return label.empty() ? "(unnamed)" : label;
		}

		std::string placeLabel(const std::string& placeId) {
			std::string label;
			query("SELECT value FROM place_names WHERE place_id = ? ORDER BY id LIMIT 1;", { placeId },
				[&](sqlite3_stmt* s) { label = columnText(s, 0); });
			return label.empty() ? placeId : label;
		}

		std::vector<Reference> loadReferences(const std::string& value, bool bySource) {
			auto filter = [&](const std::string& column) {
				return bySource ? column + " IN (SELECT id FROM citations WHERE source_id = ?)" : column + " = ?";
				};
			std::vector<Reference> out;

			query("SELECT person_id, citation_id FROM person_citations WHERE " + filter("citation_id") + ";", { value },
				[&](sqlite3_stmt* s) {
					const std::string person = columnText(s, 0);
					out.push_back({ "Person", person, personLabel(person), columnText(s, 1) });
				});

			query("SELECT f.person_id, f.type, fc.citation_id FROM person_fact_citations fc "
				"JOIN person_facts f ON f.row_key = fc.person_fact_id WHERE " + filter("fc.citation_id") + ";", { value },
				[&](sqlite3_stmt* s) {
					const std::string person = columnText(s, 0);
					out.push_back({ "Person fact", person, gedcomx::formatTypeUri(columnText(s, 1)) + " of " + personLabel(person), columnText(s, 2) });
				});

			query("SELECT r.row_key, r.type, r.person1_id, r.person2_id, rc.citation_id FROM relationship_citations rc "
				"JOIN relationships r ON r.row_key = rc.relationship_id WHERE " + filter("rc.citation_id") + ";", { value },
				[&](sqlite3_stmt* s) {
					out.push_back({ "Relationship", columnText(s, 0),
						gedcomx::formatTypeUri(columnText(s, 1)) + ": " + personLabel(columnText(s, 2)) + " & " + personLabel(columnText(s, 3)),
						columnText(s, 4) });
				});

			query("SELECT r.row_key, f.type, r.person1_id, r.person2_id, fc.citation_id FROM relationship_fact_citations fc "
				"JOIN relationship_facts f ON f.row_key = fc.relationship_fact_id "
				"JOIN relationships r ON r.row_key = f.relationship_id WHERE " + filter("fc.citation_id") + ";", { value },
				[&](sqlite3_stmt* s) {
					out.push_back({ "Relationship fact", columnText(s, 0),
						gedcomx::formatTypeUri(columnText(s, 1)) + ": " + personLabel(columnText(s, 2)) + " & " + personLabel(columnText(s, 3)),
						columnText(s, 4) });
				});

			query("SELECT e.row_key, e.type, e.description, ec.citation_id FROM event_citations ec "
				"JOIN events e ON e.row_key = ec.event_id WHERE " + filter("ec.citation_id") + ";", { value },
				[&](sqlite3_stmt* s) {
					const std::string description = columnText(s, 2);
					out.push_back({ "Event", columnText(s, 0),
						gedcomx::formatTypeUri(columnText(s, 1)) + (description.empty() ? "" : " - " + description), columnText(s, 3) });
				});

			query("SELECT e.row_key, e.type, r.type, r.person_id, rc.citation_id FROM event_role_citations rc "
				"JOIN event_roles r ON r.id = rc.event_role_id JOIN events e ON e.row_key = r.event_id "
				"WHERE " + filter("rc.citation_id") + ";", { value },
				[&](sqlite3_stmt* s) {
					out.push_back({ "Event role", columnText(s, 0),
						gedcomx::formatTypeUri(columnText(s, 2)) + " " + personLabel(columnText(s, 3)) + " in " + gedcomx::formatTypeUri(columnText(s, 1)),
						columnText(s, 4) });
				});

			query("SELECT place_id, citation_id FROM place_citations WHERE " + filter("citation_id") + ";", { value },
				[&](sqlite3_stmt* s) {
					out.push_back({ "Place", columnText(s, 0), placeLabel(columnText(s, 0)), columnText(s, 1) });
				});

			std::sort(out.begin(), out.end(), [](const Reference& a, const Reference& b) {
				return a.kind != b.kind ? a.kind < b.kind : a.label < b.label;
			});

			return out;
		}

	private:
		sqlite3* db_ = nullptr;

		// Rolls back automatically if an exception skips commit()
		struct Transaction {
			explicit Transaction(Impl& impl) : impl_(impl), name_("sp" + std::to_string(impl.savepointDepth_++)) {
				impl_.exec("SAVEPOINT " + name_ + ";");
			}
			~Transaction() {
				if (!committed_) {
					try {
						impl_.exec("ROLLBACK TO " + name_ + ";");
						impl_.exec("RELEASE " + name_ + ";");  // ROLLBACK TO alove leaves the savepoint open
					}
					catch (...) {}
				}
				--impl_.savepointDepth_;
			}
			void commit() {
				impl_.exec("RELEASE " + name_ + ";");
				committed_ = true;
			}

			Transaction(const Transaction&) = delete;
			Transaction& operator=(const Transaction&) = delete;
		private:
			Impl& impl_;
			std::string name_;
			bool committed_ = false;
		};
		int savepointDepth_ = 0;  // member of Impl, next to db_

		static std::string rowKey(const std::string& sourceId, const std::string& localId) {
			return sourceId + ":" + localId;
		}

		static std::string stripSourcePrefix(const std::string& key, const std::string& sourceId) {
			return key.substr(sourceId.size() + 1);
		}

		void ensureSchemaVersion() {
			int version = 0, tableCount = 0;

			query("PRAGMA user_version;", {}, [&](sqlite3_stmt* s) {
				version = sqlite3_column_int(s, 0);
			});
			query("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';", {},
				[&](sqlite3_stmt* s) {
					tableCount = sqlite3_column_int(s, 0);
			});

			if (tableCount > 0 && version < kSchemaVersion) {
				throw std::runtime_error("This project uses an older database format (v" + std::to_string(version) + "). Please create a new project.");
			}
			createSchema();
			if (tableCount == 0) exec("PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";");
		}

		void createSchema() {
			exec(kSchemaSql);
		}

		std::vector<gedcomx::Name> loadNamesFor(const std::string& personId) {
			std::vector<gedcomx::Name> names;

			query("SELECT id, title, nick_name, call_name FROM names WHERE person_id = ?;", { personId },
				[&](sqlite3_stmt* s) {
					long long nameId = sqlite3_column_int64(s, 0);
					gedcomx::Name name;
					name.title = columnText(s, 1);
					name.nickName = columnText(s, 2);
					name.callName = columnText(s, 3);

					query("SELECT id, full_text FROM name_forms WHERE name_id = " + std::to_string(nameId) + ";", {},
						[&](sqlite3_stmt* fs) {
							long long formId = sqlite3_column_int64(fs, 0);
							gedcomx::NameForm form;
							form.fullText = columnText(fs, 1);

							query("SELECT value, type FROM name_parts WHERE name_form_id = " + std::to_string(formId) + ";", {},
								[&](sqlite3_stmt* ps) {
									form.parts.push_back({ columnText(ps, 0), columnText(ps, 1) });
								});

							name.nameForms.push_back(std::move(form));
						});
					names.push_back(std::move(name));
				});

			return names;
		}

		std::vector<gedcomx::Fact> loadFactsForPerson(const std::string& personKey) {
			return loadFacts("person_facts", "person_fact_notes", "person_fact_citations", "person_id", personKey);
		}

		std::vector<gedcomx::Fact> loadFactsForRelationship(const std::string& relKey) {
			return loadFacts("relationship_facts", "relationship_fact_notes", "relationship_fact_citations", "relationship_id", relKey);
		}

		std::vector<gedcomx::Fact> loadFacts(const std::string& table, const std::string& notesTable, const std::string& sourcesTable, const std::string& fkColumn, const std::string& ownerKey) {
			std::vector<gedcomx::Fact> facts;
			query(std::string("SELECT row_key, local_id, type, place_original, place_description_id, value, confidence, lang, ") + kDateColumns + " FROM " + table + " WHERE " + fkColumn + " = ? ; ", { ownerKey },
				[&](sqlite3_stmt* s) {
					gedcomx::Fact fact;
					std::string factKey = columnText(s, 0);
					fact.id = columnText(s, 1);
					fact.type = columnText(s, 2);
					fact.place.original = columnText(s, 3);
					fact.place.descriptionId = columnText(s, 4);
					fact.value = columnText(s, 5);
					fact.conclusion.confidence = columnText(s, 6);
					fact.conclusion.lang = columnText(s, 7);
					fact.date = readDate(s, 8);

					std::string notesFk = (notesTable == "person_fact_notes") ? "person_fact_id" : "relationship_fact_id";
					fact.conclusion.notes = loadNotesFor(notesTable, notesFk, factKey);

					std::string sourcesFk = (sourcesTable == "person_fact_citations") ? "person_fact_id" : "relationship_fact_id";
					fact.conclusion.sources = loadCitationRefsFor(sourcesTable, sourcesFk, factKey);
					facts.push_back(std::move(fact));
				});

			return facts;
		}

		// --- small helpers so the logic above stays readable ---

		static void bindText(sqlite3_stmt* s, int idx, const std::string& v) {
			sqlite3_bind_text(s, idx, v.c_str(), -1, SQLITE_TRANSIENT);
		}

		static std::string columnText(sqlite3_stmt* s, int idx) {
			const unsigned char* text = sqlite3_column_text(s, idx);
			return text ? reinterpret_cast<const char*>(text) : "";
		}

		// First line of a statement, trimmed, so errors name the table without dumping a whole	schema
		static std::string sqlSnippet(const std::string& sql) {
			std::string s = sql.substr(0, sql.find('\n'));
			if (s.size() > 120) s = s.substr(0, 120) + "...";
			return s;
		}

		void exec(const std::string& sql) {
			char* errMsg = nullptr;
			if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
				std::string error = errMsg ? errMsg : "unknown error";
				sqlite3_free(errMsg);
				throw std::runtime_error("SQL error: " + error + " | " + sqlSnippet(sql));
			}
		}

		static void bindAll(sqlite3_stmt* s, const std::vector<std::string>& params) {
			for (size_t i = 0; i < params.size(); ++i) {
				bindText(s, static_cast<int>(i) + 1, params[i]);
			}
		}

		void exec(const std::string& sql, const std::vector<std::string>& params) {
			upsert(sql, [&](sqlite3_stmt* s) {
				bindAll(s, params);
			});
		}

		template <typename BindFn>
		void upsert(const std::string& sql, BindFn bind) {
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)) + " | " + sqlSnippet(sql));
			bind(stmt);
			if (sqlite3_step(stmt) != SQLITE_DONE) {
				std::string error = sqlite3_errmsg(db_);
				sqlite3_finalize(stmt);
				throw std::runtime_error("Failed to execute statement: " + error + " | " + sqlSnippet(sql));
			}
			sqlite3_finalize(stmt);
		}

		template <typename BindFn>
		long long insertReturningId(const std::string& sql, BindFn bind) {
			upsert(sql, bind);
			return sqlite3_last_insert_rowid(db_);
		}

		template <typename RowFn>
		void query(const std::string& sql, const std::vector<std::string>& params, RowFn onRow) {
			sqlite3_stmt* raw = nullptr;
			if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare query: " + std::string(sqlite3_errmsg(db_)) + " | " + sqlSnippet(sql));
			std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
			bindAll(stmt.get(), params);
			while (sqlite3_step(stmt.get()) == SQLITE_ROW) onRow(stmt.get());
		}
	
		// --- generic notes helpers, reused by all four owner types ---
		void saveNotesFor(const std::string& table, const std::string& fkColumn, const std::string& ownerKey, const std::vector<gedcomx::Note>& notes) {
			exec("DELETE FROM " + table + " WHERE " + fkColumn + " = ?;", { ownerKey });
			for (const auto& note : notes) {
				upsert("INSERT INTO " + table + " (" + fkColumn + ", lang, subject, text) VALUES (?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, ownerKey);
						bindText(s, 2, note.lang);
						bindText(s, 3, note.subject);
						bindText(s, 4, note.text);
					});
			}
		}

		std::vector<gedcomx::Note> loadNotesFor(const std::string& table, const std::string& fkColumn, const std::string& ownerKey) {
			std::vector<gedcomx::Note> notes;
			query("SELECT lang, subject, text FROM " + table + " WHERE " + fkColumn + " = ?;", { ownerKey },
				[&](sqlite3_stmt* s) {
					gedcomx::Note note;
					note.lang = columnText(s, 0);
					note.subject = columnText(s, 1);
					note.text = columnText(s, 2);
					notes.push_back(std::move(note));
				});

			return notes;
		}

		gedcomx::Relationship loadRelationshipRow(sqlite3_stmt* s) {
			std::string relKey = columnText(s, 0);

			gedcomx::Relationship rel;
			rel.id = columnText(s, 1);
			rel.sourceId = columnText(s, 2);
			rel.type = columnText(s, 3);
			rel.person1Id = columnText(s, 4);
			rel.person2Id = columnText(s, 5);
			rel.facts = loadFactsForRelationship(relKey);
			rel.conclusion.confidence = columnText(s, 6);
			rel.conclusion.lang = columnText(s, 7);
			rel.conclusion.notes = loadNotesFor("relationship_notes", "relationship_id", relKey);
			rel.conclusion.sources = loadCitationRefsFor("relationship_citations", "relationship_id", relKey);
			
			return rel;
		}

		bool deleteByKey(const std::string& sql, const std::string& key) {
			Transaction tx(*this);
			upsert(sql, [&](sqlite3_stmt* s) { bindText(s, 1, key); });
			const bool deleted = sqlite3_changes(db_) > 0;  // counts only the row you asked for, not cascades
			tx.commit();
			return deleted;
		}

		void saveCitationRefs(const std::string& table, const std::string& fkColumn, const std::string& ownerKey, const std::vector<gedcomx::SourceReference>& refs) {
			exec("DELETE FROM " + table + " WHERE " + fkColumn + " = ?;", { ownerKey });
			for (const auto& ref : refs) {
				upsert("INSERT INTO " + table + " (" + fkColumn + ", citation_id) VALUES (?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, ownerKey);
						bindText(s, 2, ref.citationId);
					});
			}
		}

		std::vector<gedcomx::SourceReference> loadCitationRefsFor(const std::string& table, const std::string& fkColumn, const std::string& ownerKey) {
			std::vector<gedcomx::SourceReference> refs;
			query("SELECT citation_id FROM " + table + " WHERE " + fkColumn + " = ?;", { ownerKey },
				[&](sqlite3_stmt* s) {
					refs.push_back({ columnText(s, 0) });
				});

			return refs;
		}

		// --- date helpers ---

		static constexpr const char* kDateColumns = "date_original, date_formal, date_calendar, date_quality, date_sort";
		static constexpr const char* kDatePlaceholders = "?, ?, ?, ?, ?";
		static constexpr const char* kDateUpdateSet = "date_original = excluded.date_original, date_formal = excluded.date_formal, date_calendar = excluded.date_calendar, date_quality = excluded.date_quality, date_sort = excluded.date_sort";

		void bindDate(sqlite3_stmt* s, int first, const gedcomx::Date& d) {  // first is 1-based
			bindText(s, first, d.original);
			bindText(s, first + 1, d.formal);
			bindText(s, first + 2, d.calendar);
			bindText(s, first + 3, d.quality);
			if (d.sortValue) sqlite3_bind_int64(s, first + 4, *d.sortValue);
			else sqlite3_bind_null(s, first + 4);
		}

		gedcomx::Date readDate(sqlite3_stmt* s, int first) {  // first is 0-based
			gedcomx::Date d;
			d.original = columnText(s, first);
			d.formal = columnText(s, first + 1);
			d.calendar = columnText(s, first + 2);
			d.quality = columnText(s, first + 3);
			if (sqlite3_column_type(s, first + 4) != SQLITE_NULL)
				d.sortValue = sqlite3_column_int64(s, first + 4);

			return d;
		}
	};

	// --- Database: thin forward wrapper around Impl ---

	Database::Database(const std::string& path)
		: impl_(std::make_unique<Impl>(path)) {
	}

	Database::~Database() = default;  // must be defined here, where Impl is complete

	Database::Database(Database&&) noexcept = default;
	Database& Database::operator=(Database&&) noexcept = default;

	void Database::save(const gedcomx::Person& person, const std::string& sourceId) {
		impl_->save(person, sourceId);
	}

	std::vector<gedcomx::Person> Database::loadAll() {
		return impl_->loadAll();
	}

	void Database::linkPersons(const std::string& aggregateId, const std::vector<storage::PersonRef>& refs) {
		impl_->linkPersons(aggregateId, refs);
	}

	std::vector<storage::AggregatePerson> Database::loadAggregatePersons() {
		return impl_->loadAggregatePersons();
	}

	std::optional<gedcomx::Person> Database::loadPerson(const std::string& personKey) {
		return impl_->loadPerson(personKey);
	}

	std::vector<std::string> Database::getLinkedPersonKeys(const std::string& personKey) {
		return impl_->getLinkedPersonKeys(personKey);
	}

	bool Database::deletePerson(const std::string& personKey) {
		return impl_->deletePerson(personKey);
	}

	void Database::saveRelationshipRow(const gedcomx::Relationship& relationship, const std::string& sourceId) {
		impl_->save(relationship, sourceId);
	}

	std::vector<gedcomx::Relationship> Database::loadRelationshipsForPerson(const std::string& personKey) {
		return impl_->loadRelationshipsForPerson(personKey);
	}

	std::vector<gedcomx::Relationship> Database::loadChildLinks(const std::string& parentKey) {
		return impl_->loadChildLinks(parentKey);
	}

	void Database::save(const gedcomx::Relationship& relationship, const std::string& sourceId) {
		impl_->save(relationship, sourceId);
	}

	void Database::saveAll(const std::vector<gedcomx::Relationship>& relationships, const std::string& sourceId) {
		impl_->saveAll(relationships, sourceId);
	}

	bool Database::deleteRelationship(const std::string& key) {
		return impl_->deleteRelationship(key);
	}
	
	void Database::save(const gedcomx::SourceDescription& source) {
		impl_->save(source);
	}

	std::vector<gedcomx::SourceDescription> Database::loadAllSourceDescriptions() {
		return impl_->loadAllSourceDescriptions();
	}

	void Database::save(const gedcomx::Citation& citation) {
		impl_->save(citation);
	}

	std::optional<gedcomx::Citation> Database::loadCitation(const std::string& id) {
		return impl_->loadCitation(id);
	}

	std::vector<gedcomx::Citation> Database::loadCitationsForSource(const std::string& sourceId) {
		return impl_->loadCitationsForSource(sourceId);
	}

	std::vector<gedcomx::Citation> Database::loadAllCitations() {
		return impl_->loadAllCitations();
	}

	std::string Database::ensureBareCitation(const std::string& sourceId) {
		return impl_->ensureBareCitation(sourceId);
	}

	void Database::importCitations(const gedcomx::Citation& citation) {
		// Third-party files often use short ids like "C1". An id that already belongs to a different
		// source would be silently taken over by an upsert, so refuse instead.
		if (auto existing = loadCitation(citation.id); existing && existing->sourceId != citation.sourceId) {
			throw std::runtime_error("Citation id '" + citation.id + "' already belongs to source '" + existing->sourceId
				+ "'; this file uses it for source '" + citation.sourceId + "'.");
		}
		save(citation);
	}

	void Database::save(const gedcomx::Agent& agent) {
		impl_->save(agent);
	}

	std::vector<gedcomx::Agent> Database::loadAllAgents() {
		return impl_->loadAllAgents();
	}

	void Database::save(const gedcomx::Event& event, const std::string& sourceId) {
		impl_->save(event, sourceId);
	}

	std::vector<gedcomx::Event> Database::loadAllEvents() {
		return impl_->loadAllEvents();
	}

	std::vector<gedcomx::Event> Database::loadEventsForSource(const std::string& sourceId) {
		return impl_->loadEventsForSource(sourceId);
	}
	
	void Database::save(const gedcomx::Document& document) {
		impl_->save(document);
	}

	std::vector<gedcomx::Document> Database::loadAllDocuments() {
		return impl_->loadAllDocuments();
	}

	void Database::save(const gedcomx::PlaceDescription& place) {
		impl_->save(place);
	}

	std::vector<gedcomx::PlaceDescription> Database::loadAllPlaces() {
		return impl_->loadAllPlaces();
	}

	std::vector<gedcomx::Person> Database::searchPersonsByName(const std::string& query, const std::string& sourceIdFilter, int limit) {
		return impl_->searchPersonsByName(query, sourceIdFilter, limit);
	}

	storage::SourceExport Database::gatherSourceExport(const std::string& sourceId) {
		return impl_->gatherSourceExport(sourceId);
	}

	std::string stripRowKeyPrefix(const std::string& rowKey, const std::string& sourceId) {
		return rowKey.substr(sourceId.size() + 1);
	}

	std::optional<std::string> Database::getAggregateIdForPerson(const std::string& personKey) {
		return impl_->getAggregateIdForPerson(personKey);
	}

	void Database::runInTransaction(const std::function<void()>& work) {
		impl_->runInTransaction(work);
	}

	std::vector<Reference> Database::loadCitationReferences(const std::string& citationId) {
		return impl_->loadReferences(citationId, false);
	}

	std::vector<Reference> Database::loadSourceReferences(const std::string& sourceId) {
		return impl_->loadReferences(sourceId, true);
	}
};