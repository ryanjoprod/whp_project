#include "storage/database.h"
#include "schema.h"

#include <sqlite3.h>
#include <stdexcept>
#include <map>

namespace storage {
	class Database::Impl {
	public:
		explicit Impl(const std::string& path) {
			if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
				throw std::runtime_error("Failed to open database: " + path);
			}
			createSchema();
		}

		~Impl() {
			if (db_) {
				sqlite3_close(db_);
			}
		}

		void save(const gedcomx::Person& person, const std::string& sourceId) {
			std::string key = rowKey(sourceId, person.id);
			exec("BEGIN TRANSACTION;");

			upsert("INSERT OR REPLACE INTO persons (row_key, local_id, source_id, confidence, lang) VALUES (?, ?, ?, ?, ?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, key);
					bindText(s, 2, person.id);
					bindText(s, 3, sourceId);
					bindText(s, 4, person.conclusion.confidence);
					bindText(s, 5, person.conclusion.lang);
				});

			// Clear old names for this person before re-inseting (simple
			// replace-all strategy; fine until incremental updates matter).
			exec("DELETE FROM names WHERE person_id = '" + key + "';");

			for (const auto& name : person.names) {
				long long nameId = insertReturningId(
					"INSERT INTO names (person_id) VALUES (?);",
					[&](sqlite3_stmt* s) { bindText(s, 1, key); }
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

			for (const auto& fact : person.facts) {
				std::string factKey = rowKey(key, fact.id);
				upsert("INSERT OR REPLACE INTO person_facts (row_key, local_id, person_id, type, date_original, date_formal, place_original, place_description_id, value, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, factKey);
						bindText(s, 2, fact.id);
						bindText(s, 3, key);
						bindText(s, 4, fact.type);
						bindText(s, 5, fact.date.original);
						bindText(s, 6, fact.date.formal);
						bindText(s, 7, fact.place.original);
						bindText(s, 8, fact.place.descriptionId);
						bindText(s, 9, fact.value);
						bindText(s, 10, fact.conclusion.confidence);
						bindText(s, 11, fact.conclusion.lang);
					});

				exec("DELETE FROM person_fact_sources WHERE person_fact_id = '" + factKey + "';");
				for (const auto& src : fact.conclusion.sources) {
					upsert("INSERT INTO person_fact_sources (person_fact_id, source_description_id) VALUES (?, ?);",
						[&](sqlite3_stmt* s) {
							bindText(s, 1, factKey);
							bindText(s, 2, src.sourceDescriptionId);
						});
				}

				saveNotesFor("person_fact_notes", "person_fact_id", factKey, fact.conclusion.notes);
			}

			saveNotesFor("person_notes", "person_id", key, person.conclusion.notes);

			exec("COMMIT;");
		}

		std::vector<gedcomx::Person> loadAll() {
			std::map<std::string, gedcomx::Person> people;

			// One query per person is simple to reason about; a single join
			// would be faster but harder to read at this stage of the project.
			query("SELECT row_key, local_id, source_id, confidence, lang FROM persons;",
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

			query("SELECT DISTINCT aggregate_id FROM person_links;",
				[&](sqlite3_stmt* s) {
					aggregateIds.push_back(columnText(s, 0));
				});

			std::vector<AggregatePerson> results;
			for (const auto& aggId : aggregateIds) {
				AggregatePerson agg;
				agg.aggregateId = aggId;

				// Merge names/facts from every linked person into one combined record.
				// Naive concatenation for now - no deduplication of near-identical names.
				query("SELECT person_id FROM person_links WHERE aggregate_id = '" + aggId + "';",
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

		std::optional<gedcomx::Person> loadPerson(const std::string& personKey) {
			std::optional<gedcomx::Person> result;

			query("SELECT row_key, local_id, source_id, confidence, lang FROM persons WHERE row_key = '" + personKey + "';",
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

					result = person;
				});

			return result;
		}

		void save(const gedcomx::Relationship& rel, const std::string& sourceId) {
			std::string relKey = rowKey(sourceId, rel.id);
			std::string p1Key = rowKey(sourceId, rel.person1Id);
			std::string p2Key = rowKey(sourceId, rel.person2Id);

			upsert("INSERT OR REPLACE INTO relationships (row_key, local_id, source_id, type, person1_id, person2_id, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
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

			for (const auto& fact : rel.facts) {
				std::string factKey = rowKey(relKey, fact.id);
				upsert("INSERT OR REPLACE INTO relationship_facts (row_key, local_id, relationship_id, type, date_original, date_formal, place_original, place_description_id, value, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, factKey);
						bindText(s, 2, fact.id);
						bindText(s, 3, relKey);
						bindText(s, 4, fact.type);
						bindText(s, 5, fact.date.original);
						bindText(s, 6, fact.date.formal);
						bindText(s, 7, fact.place.original);
						bindText(s, 8, fact.place.descriptionId);
						bindText(s, 9, fact.value);
						bindText(s, 10, fact.conclusion.confidence);
						bindText(s, 11, fact.conclusion.lang);
					});

				exec("DELETE FROM relationship_fact_sources WHERE relationship_fact_id = '" + factKey + "';");
				for (const auto& src : fact.conclusion.sources) {
					upsert("INSERT INTO relationship_fact_sources (relationship_fact_id, source_description_id) VALUES (? ,?);",
						[&](sqlite3_stmt* s) {
							bindText(s, 1, factKey);
							bindText(s, 2, src.sourceDescriptionId);
						});
				}

				saveNotesFor("relationship_fact_notes", "relationship_fact_id", factKey, fact.conclusion.notes);
			}

			saveNotesFor("relationship_notes", "relationship_id", relKey, rel.conclusion.notes);
		}

		std::vector<gedcomx::Relationship> loadRelationshipsForPerson(const std::string& personKey) {
			std::vector<gedcomx::Relationship> results;

			query("SELECT row_key, local_id, source_id, type, person1_id, person2_id, confidence, lang FROM relationships WHERE person1_id ='" + personKey + "' OR person2_id = '" + personKey + "';",
				[&](sqlite3_stmt* s) {
					results.push_back(loadRelationshipRow(s));
				});

			return results;
		}

		void save(const gedcomx::SourceDescription& source) {
			upsert("INSERT OR REPLACE INTO sources (id, title, citation) VALUES (?, ?, ?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, source.id);
					bindText(s, 2, source.primaryTitle());
					bindText(s, 3, source.citations.empty() ? "" : source.citations[0].value);
			});
		}

		std::vector<gedcomx::SourceDescription> loadAllSourceDescriptions() {
			std::vector<gedcomx::SourceDescription> results;

			query("SELECT id, title, citation FROM sources;",
				[&](sqlite3_stmt* s) {
					gedcomx::SourceDescription src;
					src.id = columnText(s, 0);
					src.titles.push_back({ "", columnText(s, 1) });
					src.citations.push_back({ "", columnText(s, 2) });
					results.push_back(std::move(src));
				});

			return results;
		}

		void save(const gedcomx::Agent& agent) {
			upsert("INSERT OR REPLACE INTO agents (id) VALUES (?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, agent.id);
				});

			exec("DELETE FROM agent_names WHERE agent_id = '" + agent.id + "';");
			for (const auto& name : agent.names) {
				upsert("INSERT INTO agent_names (agent_id, lang, value) VALUES (?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, agent.id);
						bindText(s, 2, name.lang);
						bindText(s, 3, name.value);
					});
			}

			exec("DELETE FROM agent_emails WHERE agent_id = '" + agent.id + "';");
			for (const auto& email : agent.emails) {
				upsert("INSERT INTO agent_emails (agent_id, email) VALUES (?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, agent.id);
						bindText(s, 2, email);
					});
			}

			exec("DELETE FROM agent_phones WHERE agent_id = '" + agent.id + "';");
			for (const auto& phone : agent.phones) {
				upsert("INSERT INTO agent_phones (agent_id, phone) VALUES (?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, agent.id);
						bindText(s, 2, phone);
					});
			}		   

			exec("DELETE FROM agent_addresses WHERE agent_id = '" + agent.id + "';");
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

			query("SELECT id FROM agents;",
				[&](sqlite3_stmt* s) {
					gedcomx::Agent agent;
					agent.id = columnText(s, 0);

					query("SELECT lang, value FROM agent_names WHERE agent_id = '" + agent.id + "';",
						[&](sqlite3_stmt* ns) {
							agent.names.push_back({ columnText(ns, 0), columnText(ns ,1) });
						});

					query("SELECT email FROM agent_emails WHERE agent_id = '" + agent.id + "';",
						[&](sqlite3_stmt* es) {
							agent.emails.push_back(columnText(es, 0));
						});

					query("SELECT phone FROM agent_phones WHERE agent_id = '" + agent.id + "';",
						[&](sqlite3_stmt* ps) {
							agent.phones.push_back(columnText(ps, 0));
						});

					query("SELECT street, city, state_or_province, postal_code, country FROM agent_addresses WHERE agent_id = '" + agent.id + "';",
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

		void save(const gedcomx::Event& event) {
			upsert("INSERT OR REPLACE INTO events (row_key, local_id, type, date_original, date_formal, place_original, confidence, lang) "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, event.id);
					bindText(s, 2, event.id);
					bindText(s, 3, event.type);
					bindText(s, 4, event.date.original);
					bindText(s, 5, event.date.formal);
					bindText(s, 6, event.place.original);
					bindText(s, 7, event.conclusion.confidence);
					bindText(s, 8, event.conclusion.lang);
				});

			exec("DELETE FROM event_roles WHERE event_id = '" + event.id + "';");
			for (const auto& role : event.roles) {
				long long roleRowId = insertReturningId(
					"INSERT INTO event_roles (event_id, type, person_id, details, confidence, lang) VALUES (?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, event.id);
						bindText(s, 2, role.type);
						bindText(s, 3, role.personId);
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
			}

			saveNotesFor("event_notes", "event_id", event.id, event.conclusion.notes);
		}

		std::vector<gedcomx::Event> loadAllEvents() {
			std::vector<gedcomx::Event> results;

			query("SELECT row_key, local_id, type, date_original, date_formal, place_original, confidence, lang FROM events;",
				[&](sqlite3_stmt* s) {
					gedcomx::Event event;
					event.id = columnText(s, 1);
					event.type = columnText(s, 2);
					event.date.original = columnText(s, 3);
					event.date.formal = columnText(s, 4);
					event.place.original = columnText(s, 5);
					event.conclusion.confidence = columnText(s, 6);
					event.conclusion.lang = columnText(s, 7);
					event.conclusion.notes = loadNotesFor("event_notes", "event_id", event.id);

					query("SELECT id, type, person_id, details, confidence, lang FROM event_roles WHERE event_id = '" + event.id + "';",
						[&](sqlite3_stmt* rs) {
							gedcomx::EventRole role;
							long long roleRowId = sqlite3_column_int64(rs, 0);
							role.type = columnText(rs, 1);
							role.personId = columnText(rs, 2);
							role.details = columnText(rs, 3);
							role.conclusion.confidence = columnText(rs, 4);
							role.conclusion.lang = columnText(rs, 5);
							role.conclusion.notes = loadNotesFor("event_role_notes", "event_role_id", std::to_string(roleRowId));
							event.roles.push_back(std::move(role));
						});

					results.push_back(std::move(event));
				});

			return results;
		}

		void save(const gedcomx::Document& doc) {
			upsert("INSERT OR REPLACE INTO documents (id, type, text_type, extracted, text, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?);",
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

			query("SELECT id, type, text_type, extracted, text, confidence, lang FROM documents;",
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
			upsert("INSERT OR REPLACE INTO places (id, type, latitude, longitude, jurisdiction_id, confidence, lang) VALUES (?, ?, ?, ?, ?, ?, ?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, place.id);
					bindText(s, 2, place.type);
					if (place.latitude) sqlite3_bind_double(s, 3, *place.latitude);
					else sqlite3_bind_null(s, 3);
					if (place.longitude) sqlite3_bind_double(s, 4, *place.longitude);
					else sqlite3_bind_null(s, 4);
					bindText(s, 5, place.jurisdictionId);
					bindText(s, 6, place.conclusion.confidence);
					bindText(s, 7, place.conclusion.lang);
				});

			exec("DELETE FROM place_names WHERE place_id = '" + place.id + "';");
			for (const auto& name : place.names) {
				upsert("INSERT INTO place_names (place_id, lang, value) VALUES (?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, place.id);
						bindText(s, 2, name.lang);
						bindText(s, 3, name.value);
					});
			}

			saveNotesFor("place_notes", "place_id", place.id, place.conclusion.notes);
		 }

		std::vector<gedcomx::PlaceDescription> loadAllPlaces() {
			std::vector<gedcomx::PlaceDescription> results;

			query("SELECT id, type, latitude, longitude, jurisdiction_id, confidence, lang FROM places;",
				[&](sqlite3_stmt* s) {
					gedcomx::PlaceDescription place;

					place.id = columnText(s, 0);
					place.type = columnText(s, 1);

					if (sqlite3_column_type(s, 2) != SQLITE_NULL) place.latitude = sqlite3_column_double(s, 2);
					if (sqlite3_column_type(s, 3) != SQLITE_NULL) place.longitude = sqlite3_column_double(s, 3);

					place.jurisdictionId = columnText(s, 4);
					place.conclusion.confidence = columnText(s, 5);
					place.conclusion.lang = columnText(s, 6);
					place.conclusion.notes = loadNotesFor("place_notes", "place_id", place.id);

					query("SELECT lang, value FROM place_names WHERE place_id = '" + place.id + "';",
						[&](sqlite3_stmt* ns) {
							place.names.push_back({ columnText(ns, 0), columnText(ns, 1) });
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
				throw std::runtime_error("Faled to prepare search query: " + std::string(sqlite3_errmsg(db_)));

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
			SourceExport result;

			query("SELECT row_key FROM persons WHERE source_id = '" + sourceId + "';",
				[&](sqlite3_stmt* s) {
					auto person = loadPerson(columnText(s, 0));
					if (person) {
						person->id = stripRowKeyPrefix(person->id, sourceId);
						result.persons.push_back(std::move(*person));
					}
			});

			query("SELECT row_key, local_id, source_id, type, person1_id, person2_id, confidence, lang FROM  relationships WHERE source_id = '" + sourceId + "';",
				[&](sqlite3_stmt* s) {
					gedcomx::Relationship rel = loadRelationshipRow(s);
					rel.person1Id = stripRowKeyPrefix(rel.person1Id, sourceId);
					rel.person2Id = stripRowKeyPrefix(rel.person2Id, sourceId);
					result.relationships.push_back(std::move(rel));
				});

			return result;
		}

	private:
		sqlite3* db_ = nullptr;

		static std::string rowKey(const std::string& sourceId, const std::string& localId) {
			return sourceId + ":" + localId;
		}

		static std::string stripSourcePrefix(const std::string& key, const std::string& sourceId) {
			return key.substr(sourceId.size() + 1);
		}

		void createSchema() {
			exec(kSchemaSql);
		}

		std::vector<gedcomx::Name> loadNamesFor(const std::string& personId) {
			std::vector<gedcomx::Name> names;

			query("SELECT id FROM names WHERE person_id = '" + personId + "';",
				[&](sqlite3_stmt* s) {
					long long nameId = sqlite3_column_int64(s, 0);
					gedcomx::Name name;

					query("SELECT id, full_text FROM name_forms WHERE name_id = " + std::to_string(nameId) + ";",
						[&](sqlite3_stmt* fs) {
							long long formId = sqlite3_column_int64(fs, 0);
							gedcomx::NameForm form;
							form.fullText = columnText(fs, 1);

							query("SELECT value, type FROM name_parts WHERE name_form_id = " + std::to_string(formId) + ";",
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
			return loadFacts("person_facts", "person_fact_notes", "person_fact_sources", "person_id", personKey);
		}

		std::vector<gedcomx::Fact> loadFactsForRelationship(const std::string& relKey) {
			return loadFacts("relationship_facts", "relationship_fact_notes", "relationship_fact_sources", "relationship_id", relKey);
		}

		std::vector<gedcomx::Fact> loadFacts(const std::string& table, const std::string& notesTable, const std::string& sourcesTable, const std::string& fkColumn, const std::string& ownerKey) {
			std::vector<gedcomx::Fact> facts;
			query("SELECT row_key, local_id, type, date_original, date_formal, place_original, place_description_id, value, confidence, lang FROM " + table + " WHERE " + fkColumn + " = '" + ownerKey + "';",
				[&](sqlite3_stmt* s) {
					gedcomx::Fact fact;
					std::string factKey = columnText(s, 0);
					fact.id = columnText(s, 1);
					fact.type = columnText(s, 2);
					fact.date.original = columnText(s, 3);
					fact.date.formal = columnText(s, 4);
					fact.place.original = columnText(s, 5);
					fact.place.descriptionId = columnText(s, 6);
					fact.value = columnText(s, 7);
					fact.conclusion.confidence = columnText(s, 8);
					fact.conclusion.lang = columnText(s, 9);

					std::string notesFk = (notesTable == "person_fact_notes") ? "person_fact_id" : "relationship_fact_id";
					fact.conclusion.notes = loadNotesFor(notesTable, notesFk, factKey);

					std::string sourcesFk = (sourcesTable == "person_fact_sources") ? "person_fact_id" : "relationship_fact_id";
					query("SELECT source_description_id FROM " + sourcesTable + " WHERE " + sourcesFk + " = '" + factKey + "';",
						[&](sqlite3_stmt* ss) {
							fact.conclusion.sources.push_back({ columnText(ss, 0) });
						});

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

		void exec(const std::string& sql) {
			char* errMsg = nullptr;
			if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
				std::string error = errMsg ? errMsg : "unknown error";
				sqlite3_free(errMsg);
				throw std::runtime_error("SQL error: " + error);
			}
		}

		template <typename BindFn>
		void upsert(const std::string& sql, BindFn bind) {
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)));
			bind(stmt);
			if (sqlite3_step(stmt) != SQLITE_DONE) {
				std::string error = sqlite3_errmsg(db_);
				sqlite3_finalize(stmt);
				throw std::runtime_error("Failed to execute statement: " + error);
			}
			sqlite3_finalize(stmt);
		}

		template <typename BindFn>
		long long insertReturningId(const std::string& sql, BindFn bind) {
			upsert(sql, bind);
			return sqlite3_last_insert_rowid(db_);
		}

		template <typename RowFn>
		void query(const std::string& sql, RowFn onRow) {
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare query: " + std::string(sqlite3_errmsg(db_)));
			while (sqlite3_step(stmt) == SQLITE_ROW) onRow(stmt);
			sqlite3_finalize(stmt);
		}
	
		// --- generic notes helpers, reused by all four owner types ---
		void saveNotesFor(const std::string& table, const std::string& fkColumn, const std::string& ownerKey, const std::vector<gedcomx::Note>& notes) {
			exec("DELETE FROM " + table + " WHERE " + fkColumn + " = '" + ownerKey + "';");
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

		std::vector<gedcomx::Note> loadNotesFor(const std::string& table, const std::string& fkColumn, const std::string ownerKey) {
			std::vector<gedcomx::Note> notes;
			query("SELECT lang, subject, text FROM " + table + " WHERE " + fkColumn + " = '" + ownerKey + "';",
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
			rel.type = columnText(s, 3);
			rel.person1Id = columnText(s, 4);
			rel.person2Id = columnText(s, 5);
			rel.facts = loadFactsForRelationship(relKey);
			rel.conclusion.confidence = columnText(s, 6);
			rel.conclusion.lang = columnText(s, 7);
			rel.conclusion.notes = loadNotesFor("relationship_notes", "relationship_id", relKey);
			
			return rel;
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

	void Database::save(const gedcomx::Relationship& relationship, const std::string& sourceId) {
		impl_->save(relationship, sourceId);
	}

	std::vector<gedcomx::Relationship> Database::loadRelationshipsForPerson(const std::string& personKey) {
		return impl_->loadRelationshipsForPerson(personKey);
	}
	
	void Database::save(const gedcomx::SourceDescription& source) {
		impl_->save(source);
	}

	std::vector<gedcomx::SourceDescription> Database::loadAllSourceDescriptions() {
		return impl_->loadAllSourceDescriptions();
	}

	void Database::save(const gedcomx::Agent& agent) {
		impl_->save(agent);
	}

	std::vector<gedcomx::Agent> Database::loadAllAgents() {
		return impl_->loadAllAgents();
	}

	void Database::save(const gedcomx::Event& event) {
		impl_->save(event);
	}

	std::vector<gedcomx::Event> Database::loadAllEvents() {
		return impl_->loadAllEvents();
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
};