#pragma once

#include "gedcomx/person.h"
#include "gedcomx/relationship.h"
#include "gedcomx/source_description.h"

#include <memory>
#include <string>
#include <vector>

namespace storage {
	struct PersonRef {
		std::string sourceId;
		std::string localId;
	};

	struct AggregatePerson {
		std::string aggregateId;
		std::vector<gedcomx::Name> names;
		std::vector<gedcomx::Fact> facts;

		std::string primaryFullText() const {
			if (!names.empty() && !names[0].nameForms.empty()) {
				return names[0].nameForms[0].fullText;
			}

			return "";
		}
	};

	class Database {
	public:
		explicit Database(const std::string& path);
		~Database();

		// Non-copyable: this class owns a raw sqlite3* handle.
		Database(const Database&) = delete;
		Database& operator=(const Database&) = delete;

		// Movable: ownership of the impl can transfer safely.
		Database(Database&&) noexcept;
		Database& operator=(Database&&) noexcept;

		void save(const gedcomx::Person& person, const std::string& sourceId);
		std::vector<gedcomx::Person> loadAll();
		void linkPersons(const std::string& aggregateId, const std::vector<PersonRef>& refs);
		std::vector<AggregatePerson> loadAggregatePersons();

		void save(const gedcomx::Relationship& relationship, const std::string& sourceId);
		std::vector<gedcomx::Relationship> loadAllRelationships();

		void save(const gedcomx::SourceDescription& source);
		std::vector<gedcomx::SourceDescription> loadAllSourceDescriptions();

	private:
		class Impl;
		std::unique_ptr<Impl> impl_;
	};
}