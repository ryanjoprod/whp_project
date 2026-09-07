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

			upsert("INSERT OR REPLACE INTO persons (row_key, local_id, source_id) VALUES (?, ?, ?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, key);
					bindText(s, 2, person.id);
					bindText(s, 3, sourceId);
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

			exec("DELETE FROM person_facts WHERE person_id = '" + key + "';");
			for (const auto& fact : person.facts) {
				upsert("INSERT INTO person_facts (local_id, person_id, type, date_original, date_formal, place_original, value) VALUES (?, ?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, fact.id);
						bindText(s, 2, key);
						bindText(s, 3, fact.type);
						bindText(s, 4, fact.date.original);
						bindText(s, 5, fact.date.formal);
						bindText(s, 6, fact.place.original);
						bindText(s, 7, fact.value);
					});
			}

			exec("COMMIT;");
		}

		std::vector<gedcomx::Person> loadAll() {
			std::map<std::string, gedcomx::Person> people;

			// One query per person is simple to reason about; a single join
			// would be faster but harder to read at this stage of the project.
			query("SELECT row_key, local_id FROM persons;",
				[&](sqlite3_stmt* s) {
					std::string key = columnText(s, 0);
					std::string localId = columnText(s, 1);
					people[key] = gedcomx::Person{ key, loadNamesFor(key), loadFactsForPerson(key) };
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

		void save(const gedcomx::Relationship& rel, const std::string& sourceId) {
			std::string relKey = rowKey(sourceId, rel.id);
			std::string p1Key = rowKey(sourceId, rel.person1Id);
			std::string p2Key = rowKey(sourceId, rel.person2Id);

			upsert("INSERT OR REPLACE INTO relationships (row_key, local_id, source_id, type, person1_id, person2_id) VALUES (?, ?, ?, ?, ?, ?);",
				[&](sqlite3_stmt* s) {
					bindText(s, 1, relKey);
					bindText(s, 2, rel.id);
					bindText(s, 3, sourceId);
					bindText(s, 4, rel.type);
					bindText(s, 5, p1Key);
					bindText(s, 6, p2Key);
				});

			exec("DELETE FROM relationship_facts WHERE relationship_id = '" + relKey + "';");
			for (const auto& fact : rel.facts) {
				upsert("INSERT INTO relationship_facts (local_id, relationship_id, type, date_original, date_formal, place_original, value) VALUES (?, ?, ?, ?, ?, ?, ?);",
					[&](sqlite3_stmt* s) {
						bindText(s, 1, fact.id);
						bindText(s, 2, relKey);
						bindText(s, 3, fact.type);
						bindText(s, 4, fact.date.original);
						bindText(s, 5, fact.date.formal);
						bindText(s, 6, fact.place.original);
						bindText(s, 7, fact.value);
					});
			}

		}

		std::vector<gedcomx::Relationship> loadAllRelationships() {
			std::vector<gedcomx::Relationship> results;
			query("SELECT row_key, local_id, source_id, type, person1_id, person2_id FROM relationships;",
				[&](sqlite3_stmt* s) {
					gedcomx::Relationship rel;
					std::string relKey = columnText(s, 0);
					rel.id = columnText(s, 1);
					// std::string sourceId = columnText(s, 2);
					rel.type = columnText(s, 3);
					rel.person1Id = columnText(s, 4);
					rel.person2Id = columnText(s, 5);
					rel.facts = loadFactsForRelationship(relKey);
					results.push_back(std::move(rel));
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
			return loadFacts("person_facts", "person_id", personKey);
		}

		std::vector<gedcomx::Fact> loadFactsForRelationship(const std::string& relKey) {
			return loadFacts("relationship_facts", "relationship_id", relKey);
		}

		std::vector<gedcomx::Fact> loadFacts(const std::string& table, const std::string& fkColumn, const std::string& ownerKey) {
			std::vector<gedcomx::Fact> facts;
			query("SELECT local_id, type, date_original, date_formal, place_original, value FROM " + table + " WHERE " + fkColumn + " = '" + ownerKey + "';",
				[&](sqlite3_stmt* s) {
					gedcomx::Fact fact;
					fact.id = columnText(s, 0);
					fact.type = columnText(s, 1);
					fact.date.original = columnText(s, 2);
					fact.date.formal = columnText(s, 3);
					fact.place.original = columnText(s, 4);
					fact.value = columnText(s, 5);
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

	void Database::save(const gedcomx::Relationship& relationship, const std::string& sourceId) {
		impl_->save(relationship, sourceId);
	}

	std::vector<gedcomx::Relationship> Database::loadAllRelationships() {
		return impl_->loadAllRelationships();
	}
	
	void Database::save(const gedcomx::SourceDescription& source) {
		impl_->save(source);
	}

	std::vector<gedcomx::SourceDescription> Database::loadAllSourceDescriptions() {
		return impl_->loadAllSourceDescriptions();
	}
};