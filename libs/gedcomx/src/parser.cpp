#include "gedcomx/parser.h"

#include <string>

namespace {
	// GEDCOM X references look like "#P1" - the '#' means "local id in this
	// same document." Strip it since we store bare ids ourselves.
	std::string stripResourceRef(const std::string& ref) {
		return (!ref.empty() && ref[0] == '#') ? ref.substr(1) : ref;
	}
}

namespace gedcomx {
	Person parsePerson(const nlohmann::json& j) {
		Person person;
		person.id = j.value("id", "");

		if (j.contains("names")) {
			for (const auto& nameJson : j.at("names")) {
				Name name;

				if (nameJson.contains("nameForms")) {
					for (const auto& formJson : nameJson.at("nameForms")) {
						NameForm form;
						form.fullText = formJson.value("fullText", "");

						if (formJson.contains("parts")) {
							for (const auto& partJson : formJson.at("parts")) {
								NamePart part;
								part.value = partJson.value("value", "");
								part.type = partJson.value("type", "");
								form.parts.push_back(std::move(part));
							}
						}

						name.nameForms.push_back(std::move(form));
					}
				}

				person.names.push_back(std::move(name));
			}
		}
		if (j.contains("facts")) {
			for (const auto& factJson : j.at("facts")) {
				person.facts.push_back(parseFact(factJson));
			}
		}

		return person;
	}

	std::vector<Person> parsePersons(const nlohmann::json& document) {
		std::vector<Person> people;
		if (document.contains("persons")) {
			for (const auto& personJson : document.at("persons")) {
				people.push_back(parsePerson(personJson));
			}
		}

		return people;
	}

	Relationship parseRelationship(const nlohmann::json& j) {
		Relationship rel;
		rel.id = j.value("id", "");
		rel.type = j.value("type", "");

		if (j.contains("person1")) {
			rel.person1Id = stripResourceRef(j.at("person1").value("resource", ""));
		}
		if (j.contains("person2")) {
			rel.person2Id = stripResourceRef(j.at("person2").value("resource", ""));
		}
		if (j.contains("facts")) {
			for (const auto& factJson : j.at("facts")) {
				rel.facts.push_back(parseFact(factJson));
			}
		}

		return rel;
	}

	std::vector<Relationship> parseRelationships(const nlohmann::json& document) {
		std::vector<Relationship> relationships;
		if (document.contains("relationships")) {
			for (const auto& relJson : document.at("relationships")) {
				relationships.push_back(parseRelationship(relJson));
			}
		}

		return relationships;
	}

	SourceDescription parseSourceDescription(const nlohmann::json& j) {
		SourceDescription source;
		source.id = j.value("id", "");

		if (j.contains("titles")) {
			for (const auto& t : j.at("titles")) {
				source.titles.push_back({ t.value("lang", ""), t.value("value", "") });
			}
		}

		if (j.contains("citations")) {
			for (const auto& c : j.at("citations")) {
				source.citations.push_back({ c.value("lang", ""), c.value("value", "") });
			}
		}

		return source;
	}

	std::vector<SourceDescription> parseSourceDescriptions(const nlohmann::json& document) {
		std::vector<SourceDescription> sources;

		if (document.contains("sourceDescriptions")) {
			for (const auto& j : document.at("sourceDescriptions")) {
				sources.push_back(parseSourceDescription(j));
			}
		}

		return sources;
	}

	Fact parseFact(const nlohmann::json& j) {
		Fact fact;
		fact.id = j.value("id", "");
		fact.type = j.value("type", "");
		fact.value = j.value("value", "");

		if (j.contains("date")) {
			fact.date.original = j.at("date").value("original", "");
			fact.date.formal = j.at("date").value("formal", "");
		}

		if (j.contains("place")) {
			fact.place.original = j.at("place").value("original", "");
		}

		return fact;
	}

	std::string formatFactType(const std::string& typeUri) {
		const std::string prefix = "http://gedcomx.org/";
		if (typeUri.rfind(prefix, 0) == 0) {
			return typeUri.substr(prefix.size());
		}

		return typeUri;  // unrecognized format - show it as-is rather than hide it
	}
}