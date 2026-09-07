#pragma once

#include "gedcomx/person.h"
#include "gedcomx/relationship.h"  
#include "gedcomx/source_description.h"
#include "gedcomx/fact.h"

#include <nlohmann/json.hpp>

#include <vector>

namespace gedcomx {
	Person parsePerson(const nlohmann::json& j);
	std::vector<Person> parsePersons(const nlohmann::json& document);

	Relationship parseRelationship(const nlohmann::json& j);
	std::vector<Relationship> parseRelationships(const nlohmann::json& document);

	SourceDescription parseSourceDescription(const nlohmann::json& j);
	std::vector<SourceDescription> parseSourceDescriptions(const nlohmann::json& document);

	Fact parseFact(const nlohmann::json& j);
}