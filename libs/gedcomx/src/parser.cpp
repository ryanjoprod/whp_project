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

		person.conclusion = parseConclusion(j);

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

		rel.conclusion = parseConclusion(j);

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
			if (j.at("place").contains("description")) {
				fact.place.descriptionId = stripResourceRef(j.at("place").at("description").value("resource", ""));
			}
		}

		fact.conclusion = parseConclusion(j);

		return fact;
	}

	std::string formatTypeUri(const std::string& typeUri) {
		const std::string prefix = "http://gedcomx.org/";
		if (typeUri.rfind(prefix, 0) == 0) {
			return typeUri.substr(prefix.size());
		}

		return typeUri;  // unrecognized format - show it as-is rather than hide it
	}

	Conclusion parseConclusion(const nlohmann::json& j) {
		Conclusion conclusion;
		conclusion.confidence = j.value("confidence", "");
		conclusion.lang = j.value("lang", "");

		if (j.contains("sources")) {
			for (const auto& srcJson : j.at("sources")) {
				SourceReference ref;
				ref.sourceDescriptionId = stripResourceRef(srcJson.value("description", nlohmann::json::object()).value("resource", ""));
				conclusion.sources.push_back(std::move(ref));
			}
		}

		if (j.contains("notes")) {
			for (const auto& noteJson : j.at("notes")) {
				Note note;
				note.lang = noteJson.value("lang", "");
				note.subject = noteJson.value("subject", "");
				note.text = noteJson.value("text", "");

				conclusion.notes.push_back(std::move(note));
			}
		}

		return conclusion;
	}

	Agent parseAgent(const nlohmann::json& j) {
		Agent agent;
		agent.id = j.value("id", "");

		if (j.contains("names")) {
			for (const auto& n : j.at("names")) {
				agent.names.push_back({ n.value("lang", ""), n.value("value", "") });
			}
		}

		if (j.contains("emails")) {
			for (const auto& e : j.at("emails")) {
				agent.emails.push_back(e.value("resource", ""));
			}
		}

		if (j.contains("phones")) {
			for (const auto& p : j.at("phones")) {
				agent.phones.push_back(p.value("resource", ""));
			}
		}

		if (j.contains("addresses")) {
			for (const auto& a : j.at("addresses")) {
				Address addr;

				addr.street = a.value("street", "");
				addr.city = a.value("city", "");
				addr.stateOrProvince = a.value("stateOrProvidence", "");
				addr.postalCode = a.value("postalCode", "");
				addr.country = a.value("country", "");

				agent.addresses.push_back(std::move(addr));
			}
		}

		return agent;
	}

	std::vector<Agent> parseAgents(const nlohmann::json& document) {
		std::vector<Agent> agents;

		if (document.contains("agents")) {
			for (const auto& j : document.at("agents")) {
				agents.push_back(parseAgent(j));
			}
		}

		return agents;
	}

	EventRole parseEventRole(const nlohmann::json& j) {
		EventRole role;
		role.type = j.value("type", "");
		role.details = j.value("details", "");

		if (j.contains("person")) {
			role.personId = stripResourceRef(j.at("person").value("resource", ""));
		}

		role.conclusion = parseConclusion(j);

		return role;
	}

	Event parseEvent(const nlohmann::json& j) {
		Event event;
		event.id = j.value("id", "");
		event.type = j.value("type", "");

		if (j.contains("date")) {
			event.date.original = j.at("date").value("original", "");
			event.date.formal = j.at("date").value("formal", "");
		}

		if (j.contains("place")) {
			event.place.original = j.at("place").value("original", "");
			if (j.at("place").contains("description")) {
				event.place.descriptionId = stripResourceRef(j.at("place").at("description").value("resource", ""));
			}
		}

		if (j.contains("roles")) {
			for (const auto& roleJson : j.at("roles")) {
				event.roles.push_back(parseEventRole(roleJson));
			}
		}

		event.conclusion = parseConclusion(j);

		return event;
	}

	std::vector<Event> parseEvents(const nlohmann::json& document) {
		std::vector<Event> events;
		if (document.contains("events")) {
			for (const auto& j : document.at("events")) {
				events.push_back(parseEvent(j));
			}
		}

		return events;
	}

	Document parseDocument(const nlohmann::json& j) {
		Document doc;

		doc.id = j.value("id", "");
		doc.type = j.value("type", "");
		doc.textType = j.value("textType", "");
		doc.extracted = j.value("extracted", false);
		doc.text = j.value("text", "");
		doc.conclusion = parseConclusion(j);

		return doc;
	}

	std::vector<Document> parseDocuments(const nlohmann::json& document) {
		std::vector<Document> docs;
		if (document.contains("documents")) {
			for (const auto& j : document.at("documents")) {
				docs.push_back(parseDocument(j));
			}
		}

		return docs;
	}

	PlaceDescription parsePlaceDescription(const nlohmann::json& j) {
		PlaceDescription place;
		place.id = j.value("id", "");
		place.type = j.value("type", "");

		if (j.contains("names")) {
			for (const auto& n : j.at("names")) {
				place.names.push_back({ n.value("lang", ""), n.value("value", "") });
			}
		}

		if (j.contains("latitude")) place.latitude = j.at("latitude").get<double>();
		if (j.contains("longitude")) place.longitude = j.at("longitude").get<double>();

		if (j.contains("jurisdiction")) {
			place.jurisdictionId = stripResourceRef(j.at("jurisdiction").value("resource", ""));
		}

		place.conclusion = parseConclusion(j);

		return place;
	}

	std::vector<PlaceDescription> parsePlaceDescriptions(const nlohmann::json& document) {
		std::vector<PlaceDescription> places;

		if (document.contains("places")) {
			for (const auto& j : document.at("places")) {
				places.push_back(parsePlaceDescription(j));
			}
		}

		return places;
	}

	// --- Serializers ---

	nlohmann::json serializeConclusion(const gedcomx::Conclusion& conclusion) {
		nlohmann::json j;
		if (!conclusion.confidence.empty()) j["confidence"] = conclusion.confidence;
		if (!conclusion.lang.empty()) j["lang"] = conclusion.lang;

		if (!conclusion.sources.empty()) {
			nlohmann::json sources = nlohmann::json::array();
			for (const auto& src : conclusion.sources) {
				sources.push_back({ {"description", {{"resource", "#" + src.sourceDescriptionId}}} });
			}
			j["sources"] = sources;
		}

		if (!conclusion.notes.empty()) {
			nlohmann::json notes = nlohmann::json::array();
			for (const auto& note : conclusion.notes) {
				nlohmann::json n;
				if (!note.lang.empty()) n["lang"] = note.lang;
				if (!note.subject.empty()) n["subject"] = note.subject;
				n["text"] = note.text;
				notes.push_back(n);
			}
			j["notes"] = notes;
		}

		return j;
	}

	nlohmann::json serializeFact(const gedcomx::Fact& fact) {
		nlohmann::json j = serializeConclusion(fact.conclusion);
		j["id"] = fact.id;
		j["type"] = fact.type;

		if (!fact.date.original.empty() || !fact.date.formal.empty()) {
			nlohmann::json date;
			if (!fact.date.original.empty()) date["original"] = fact.date.original;
			if (!fact.date.formal.empty()) date["formal"] = fact.date.formal;
			j["date"] = date;
		}

		if (!fact.place.original.empty() || !fact.place.descriptionId.empty()) {
			nlohmann::json place;
			if (!fact.place.original.empty()) place["original"] = fact.place.original;
			if (!fact.place.descriptionId.empty()) {
				place["description"] = { {"resource", "#" + fact.place.descriptionId} };
			}
			j["place"] = place;
		}

		if (!fact.value.empty()) j["value"] = fact.value;

		return j;
	}

	nlohmann::json serializePerson(const gedcomx::Person& person) {
		nlohmann::json j = serializeConclusion(person.conclusion);
		j["id"] = person.id;

		if (!person.names.empty()) {
			nlohmann::json names = nlohmann::json::array();
			for (const auto& name : person.names) {
				nlohmann::json nameForms = nlohmann::json::array();
				for (const auto& form : name.nameForms) {
					nlohmann::json formJson;
					if (!form.fullText.empty()) formJson["fullText"] = form.fullText;

					if (!form.parts.empty()) {
						nlohmann::json parts = nlohmann::json::array();
						for (const auto& part : form.parts) {
							parts.push_back({ {"value", part.value}, {"type", part.type} });
						}
						formJson["parts"] = parts;
					}
					nameForms.push_back(formJson);
				}
				names.push_back({ {"nameForms", nameForms} });
			}
			j["names"] = names;
		}

		if (!person.facts.empty()) {
			nlohmann::json facts = nlohmann::json::array();
			for (const auto& fact : person.facts) {
				facts.push_back(serializeFact(fact));
			}
			j["facts"] = facts;
		}

		return j;
	}

	nlohmann::json serializeRelationship(const gedcomx::Relationship& rel) {
		nlohmann::json j = serializeConclusion(rel.conclusion);
		j["id"] = rel.id;
		j["type"] = rel.type;
		j["person1"] = { {"resource", "#" + rel.person1Id} };
		j["person2"] = { {"resource", "#" + rel.person2Id} };

		if (!rel.facts.empty()) {
			nlohmann::json facts = nlohmann::json::array();
			for (const auto& fact : rel.facts) {
				facts.push_back(serializeFact(fact));
			}
			j["facts"] = facts;
		}

		return j;
	}

	nlohmann::json serializeSourceDescription(const gedcomx::SourceDescription& source) {
		nlohmann::json j;
		j["id"] = source.id;

		if (!source.titles.empty()) {
			nlohmann::json titles = nlohmann::json::array();
			for (const auto& t : source.titles) {
				nlohmann::json tj;
				if (!t.lang.empty()) tj["lang"] = t.lang;
				tj["value"] = t.value;
				titles.push_back(tj);
			}
			j["titles"] = titles;
		}

		if (!source.citations.empty()) {
			nlohmann::json citations = nlohmann::json::array();
			for (const auto& c : source.citations) {
				nlohmann::json cj;
				if (!c.lang.empty()) cj["lang"] = c.lang;
				cj["value"] = c.value;
				citations.push_back(cj);
			}
			j["citations"] = citations;
		}

		return j;
	}

	nlohmann::json serializeSourceExport(const gedcomx::SourceDescription& source, const std::vector<gedcomx::Person>& persons, const std::vector<gedcomx::Relationship>& relationships) {
		nlohmann::json doc;

		doc["sourceDescriptions"] = nlohmann::json::array({ serializeSourceDescription(source) });

		nlohmann::json personsJson = nlohmann::json::array();
		for (const auto& p : persons) personsJson.push_back(serializePerson(p));
		doc["persons"] = personsJson;

		nlohmann::json relsJson = nlohmann::json::array();
		for (const auto& r : relationships) relsJson.push_back(serializeRelationship(r));
		doc["relationships"] = relsJson;

		return doc;
	}
}