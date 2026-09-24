#pragma once

#include <nlohmann/json.hpp>

#include <vector>

#include "gedcomx/person.h"
#include "gedcomx/relationship.h"  
#include "gedcomx/source_description.h"
#include "gedcomx/fact.h"
#include "gedcomx/conclusion.h"
#include "gedcomx/agent.h"
#include "gedcomx/event.h"
#include "gedcomx/document.h"
#include "gedcomx/place_description.h"

namespace gedcomx {
	Person parsePerson(const nlohmann::json& j);
	std::vector<Person> parsePersons(const nlohmann::json& document);

	Relationship parseRelationship(const nlohmann::json& j);
	std::vector<Relationship> parseRelationships(const nlohmann::json& document);

	SourceDescription parseSourceDescription(const nlohmann::json& j);
	std::vector<SourceDescription> parseSourceDescriptions(const nlohmann::json& document);

	Fact parseFact(const nlohmann::json& j);

	Conclusion parseConclusion(const nlohmann::json& j);

	Agent parseAgent(const nlohmann::json& j);
	std::vector<Agent> parseAgents(const nlohmann::json& document);

	EventRole parseEventRole(const nlohmann::json& j);
	Event parseEvent(const nlohmann::json& j);
	std::vector<Event> parseEvents(const nlohmann::json& document);

	Document parseDocument(const nlohmann::json& j);
	std::vector<Document> parseDocuments(const nlohmann::json& document);

	PlaceDescription parsePlaceDescription(const nlohmann::json& j);
	std::vector<PlaceDescription> parsePlaceDescriptions(const nlohmann::json& document);

	// --- Serializers ---

	nlohmann::json serializeConclusion(const gedcomx::Conclusion& conclusion);
	nlohmann::json serializeFact(const gedcomx::Fact& fact);
	nlohmann::json serializePerson(const gedcomx::Person& person);
	nlohmann::json serializeRelationship(const gedcomx::Relationship& rel);
	nlohmann::json serializeSourceDescription(const gedcomx::SourceDescription& source);
	nlohmann::json serializeSourceExport(const gedcomx::SourceDescription& source, const std::vector<gedcomx::Person>& persons, const std::vector<gedcomx::Relationship>& relationships);
}