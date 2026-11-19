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
#include "gedcomx/citation.h"

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
		std::vector<gedcomx::SourceDescription> otherSources;  // other sources the export cites,so the files is self-contained
		std::vector<gedcomx::Citation> citations;  // real citations only; bare ones are just "#S1" on a reference
		std::vector<gedcomx::Event> events;
	};

	struct Reference {
		std::string kind;  // "Person", "Person fact", "Relationship", "Relationship fact", "Event", "Event role", "Place"
		std::string ownerKey;  // row key or id of the citing record; kept so a later step can jump to it
		std::string label;  // readable, e.g. "Birth of Abraham", "Marriage: Abraham & Sarah"
		std::string citationId;  // which citation it cites; tells a source's list which one 
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
		std::vector<gedcomx::Relationship> loadChildLinks(const std::string& parentKey);
		void save(const gedcomx::Relationship& relationship, const std::string& sourceId);
		void saveAll(const std::vector<gedcomx::Relationship>& relationships, const std::string& sourceId);
		bool deleteRelationship(const std::string& relationshipKey);

		void save(const gedcomx::SourceDescription& source);
		std::vector<gedcomx::SourceDescription> loadAllSourceDescriptions();

		void save(const gedcomx::Citation& citation);  // upsert, notes replaced
		std::optional<gedcomx::Citation> loadCitation(const std::string& id);
		std::vector<gedcomx::Citation> loadCitationsForSource(const std::string& sourceId);
		std::vector<gedcomx::Citation> loadAllCitations();
		// Creates the source's bare citation if it doesn't exist yet; returns its id. Safe to call repeatedly
		std::string ensureBareCitation(const std::string& sourceId);
		void importCitations(const gedcomx::Citation& citation);  // like save(), but refuses to take over another source's citation id

		void save(const gedcomx::Agent& agent);
		std::vector<gedcomx::Agent> loadAllAgents();

		void save(const gedcomx::Event& event, const std::string& sourceId);
		std::vector<gedcomx::Event> loadAllEvents();
		std::vector<gedcomx::Event> loadEventsForSource(const std::string& sourceId);

		void save(const gedcomx::Document& document);
		std::vector<gedcomx::Document> loadAllDocuments();

		void save(const gedcomx::PlaceDescription& place);
		std::vector<gedcomx::PlaceDescription> loadAllPlaces();

		std::vector<gedcomx::Event> events;  // events owned by this source; ids and role persons are local, prefix stripped 

		std::vector<Reference> loadCitationReferences(const std::string& citationId);
		std::vector<Reference> loadSourceReferences(const std::string& sourceId);  // every citation of the source, bare one included

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