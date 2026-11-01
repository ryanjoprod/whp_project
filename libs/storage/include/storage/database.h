#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>

#include "gedcomx/person.h"
#include "gedcomx/relationship.h"
#include "gedcomx/source_description.h"
#include "gedcomx/agent.h"
#include "gedcomx/event.h"
#include "gedcomx/document.h"
#include "gedcomx/place_description.h"

namespace storage {
	// Strips a known sourceId prefix off a row-key to recover the bare local id
	// (e.g. stripRowKeyPrefix("S1:P1", "S1") -> "P1"). Used anywhere a Person's
	// or Relationship's row-key needs converting back to its original GEDCOM X
	// local id - for creating new relationships/facts, or exporting data.
	std::string stripRowKeyPrefix(const std::string& rowKey, const std::string& sourceId);

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

	struct SourceExport {
		std::vector<gedcomx::Person> persons;
		std::vector<gedcomx::Relationship> relationships;
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
		std::optional<gedcomx::Person> loadPerson(const std::string& personKey);
		void linkPersons(const std::string& aggregateId, const std::vector<PersonRef>& refs);
		std::vector<AggregatePerson> loadAggregatePersons();
		std::optional<std::string> getAggregateIdForPerson(const std::string& personKey);
		bool areSamePerson(const std::string& personKeyA, const std::string& personKeyB);
		std::vector<std::string> getLinkedPersonKeys(const std::string& personKey);
		bool deletePerson(const std::string& personKey);

		void saveRelationshipRow(const gedcomx::Relationship& relationship, const std::string& sourceId);
		std::vector<gedcomx::Relationship> loadRelationshipsForPerson(const std::string& personKey);
		void save(const gedcomx::Relationship& relationship, const std::string& sourceId);
		void saveAll(const std::vector<gedcomx::Relationship>& relationships, const std::string& sourceId);
		bool deleteRelationship(const std::string& relationshipKey);

		void save(const gedcomx::SourceDescription& source);
		std::vector<gedcomx::SourceDescription> loadAllSourceDescriptions();

		void save(const gedcomx::Agent& agent);
		std::vector<gedcomx::Agent> loadAllAgents();

		void save(const gedcomx::Event& event);
		std::vector<gedcomx::Event> loadAllEvents();

		void save(const gedcomx::Document& document);
		std::vector<gedcomx::Document> loadAllDocuments();

		void save(const gedcomx::PlaceDescription& place);
		std::vector<gedcomx::PlaceDescription> loadAllPlaces();

		// Requests methods
		std::vector<gedcomx::Person> searchPersonsByName(const std::string& query, const std::string& sourceIdFilter = "", int limit = 20);

		// Exports
		SourceExport gatherSourceExport(const std::string& sourceId);

		void runInTransaction(const std::function<void()>& work);

	private:
		class Impl;
		std::unique_ptr<Impl> impl_;
	};
}