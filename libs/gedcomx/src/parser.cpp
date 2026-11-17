#include "gedcomx/parser.h"
#include "gedcomx/citation.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
	// GEDCOM X references look like "#P1" - the '#' means "local id in this
	// same document." Strip it since we store bare ids ourselves.
	std::string stripResourceRef(const std::string& ref) {
		return (!ref.empty() && ref[0] == '#') ? ref.substr(1) : ref;
	}

	// component id -> id of the description it is a component of. Only counts parents that	are in the document.
	std::unordered_map<std::string, std::string> componentParents(const nlohmann::json& document) {
		std::unordered_set<std::string> ids;
		for (const auto& j : document.at("sourceDescriptions"))
			ids.insert(j.value("id", ""));

		std::unordered_map<std::string, std::string> parents;
		for (const auto& j : document.at("sourceDescriptions")) {
			if (!j.contains("componentOf")) continue;
			const std::string id = j.value("id", "");
			const std::string parent = stripResourceRef(j.at("componentOf").value("description", ""));
			if (!id.empty() && parent != id && ids.count(parent)) parents[id] = parent;
		}

		return parents;
	}

	// Follows componentOf up to the real source. Empty if the chain loops.
	std::string rootSourceId(const std::unordered_map<std::string, std::string>& parents, const std::string& id) {
		std::string current = id;
		for (size_t steps = 0; steps <= parents.size(); ++steps) {  // the cap stops a cycle from looping forever
			auto it = parents.find(current);
			if (it == parents.end()) return current;
			current = it->second;
		}

		return "";
	}

	void remapRefs(std::vector<gedcomx::SourceReference>& refs, const std::unordered_set<std::string>& citationIds) {
		for (auto& ref : refs) {
			auto sourceId = gedcomx::bareCitationSourceId(ref.citationId);
			if (sourceId && citationIds.count(*sourceId))
				ref.citationId = *sourceId;
		}
	}

	gedcomx::Address parseAddress(const nlohmann::json& a) {
		gedcomx::Address addr;

		addr.id = a.value("id", "");
		addr.street = a.value("street", "");
		addr.city = a.value("city", "");
		addr.stateOrProvince = a.value("stateOrProvince", "");
		addr.postalCode = a.value("postalCode", "");
		addr.country = a.value("country", "");

		return addr;
	}

	nlohmann::json serializeAddress(const gedcomx::Address& addr) {
		nlohmann::json j;
		if (!addr.id.empty()) j["id"] = addr.id;
		if (!addr.street.empty()) j["street"] = addr.street;
		if (!addr.city.empty()) j["city"] = addr.city;
		if (!addr.stateOrProvince.empty()) j["stateOrProvince"] = addr.stateOrProvince;
		if (!addr.postalCode.empty()) j["postalCode"] = addr.postalCode;
		if (!addr.country.empty()) j["country"] = addr.country;

		return j;
	}

	nlohmann::json serializeNotes(const std::vector<gedcomx::Note>& notes) {
		nlohmann::json out = nlohmann::json::array();
		for (const auto& note : notes) {
			nlohmann::json n;
			if (!note.lang.empty()) n["lang"] = note.lang;
			if (!note.subject.empty()) n["subject"] = note.subject;
			n["text"] = note.text;
			out.push_back(n);
		}

		return out;
	}

	std::vector<gedcomx::Note> parseNotes(const nlohmann::json& j) {
		std::vector<gedcomx::Note> notes;
		if (j.contains("notes")) {
			for (const auto& noteJson : j.at("notes")) {
				gedcomx::Note note;
				note.lang = noteJson.value("lang", "");
				note.subject = noteJson.value("subject", "");
				note.text = noteJson.value("text", "");
				notes.push_back(std::move(note));
			}
		}

		return notes;
	}

	bool isEmptyDate(const gedcomx::Date& d) {
		return d.original.empty() && d.formal.empty() && d.calendar.empty() && d.quality.empty() && !d.sortValue;
	}

	gedcomx::Date parseDate(const nlohmann::json j) {
		gedcomx::Date d;
		d.original = j.value("original", "");
		d.formal = j.value("formal", "");
		d.calendar = j.value("calendar", "");  // app extensions; absent in files from other programs
		d.quality = j.value("quality", "");
		if (j.contains("sortValue") && j.at("sortValue").is_number_integer())
			d.sortValue = j.at("sortValue").get<long long>();
		return d;
	}

	nlohmann::json serializeDate(const gedcomx::Date& d) {
		nlohmann::json j = nlohmann::json::object();
		if (!d.original.empty()) j["original"] = d.original;
		if (!d.formal.empty()) j["formal"] = d.formal;
		if (!d.calendar.empty()) j["calendar"] = d.calendar;
		if (!d.quality.empty()) j["quality"] = d.quality;
		if (d.sortValue) j["sortValue"] = *d.sortValue;
		return j;
	}

	std::vector<gedcomx::Attribute> parseAttributes(const nlohmann::json& j) {
		std::vector<gedcomx::Attribute> out;
		if (j.contains("attributes")) {
			for (const auto& a : j.at("attributes"))
				out.push_back({ a.value("id", ""), a.value("type", ""), a.value("value", "") });
		}
		return out;
	}

	nlohmann::json serializeAttributes(const std::vector<gedcomx::Attribute> attrs) {
		nlohmann::json out = nlohmann::json::array();
		for (const auto& a : attrs) out.push_back({ { "id", a.id },{ "type", a.type },{ "value", a.value } });
		return out;
	}
}

namespace gedcomx {
	Person parsePerson(const nlohmann::json& j) {
		Person person;
		person.id = j.value("id", "");

		if (j.contains("names")) {
			for (const auto& nameJson : j.at("names")) {
				Name name;
				name.title = nameJson.value("title", "");
				name.nickName = nameJson.value("nickName", "");
				name.callName = nameJson.value("callName", "");

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

		if (j.contains("gender")) {
			const std::string type = j.at("gender").value("type", "");
			if (!type.empty()) person.gender = Gender{ type };
		}

		person.attributes = parseAttributes(j);

		if (j.contains("addresses")) {
			for (const auto& a : j.at("addresses"))
				person.addresses.push_back(parseAddress(a));
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

		source.author = j.value("author", "");
		source.publication = j.value("publication", "");
		source.abbreviation = j.value("abbreviation", "");
		source.notes = parseNotes(j);
		source.attributes = parseAttributes(j);

		return source;
	}

	std::vector<SourceDescription> parseSourceDescriptions(const nlohmann::json& document) {
		std::vector<SourceDescription> sources;

		if (document.contains("sourceDescriptions")) {
			const auto components = componentParents(document);
			for (const auto& j : document.at("sourceDescriptions")) {
				if (components.count(j.value("id", ""))) continue;  // a citation; see parseCitations
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

		if (j.contains("date")) fact.date = parseDate(j.at("date"));

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
				const std::string descId = stripResourceRef(srcJson.value("description", nlohmann::json::object()).value("resource", ""));
				if (descId.empty()) continue;
				SourceReference ref;
				ref.citationId = gedcomx::bareCitationId(descId);
				conclusion.sources.push_back(std::move(ref));
			}
		}

		conclusion.notes = parseNotes(j);

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
				agent.addresses.push_back(parseAddress(a));
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
		event.description = j.value("description", "");

		if (j.contains("date")) event.date = parseDate(j.at("date"));

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
			const std::string parent = stripResourceRef(j.at("jurisdiction").value("resource", ""));
			if (!parent.empty()) place.jurisdictions.push_back({ parent,{} });
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

	std::vector<Citation> parseCitations(const nlohmann::json& document) {
		std::vector<Citation> citations;
		if (!document.contains("sourceDescriptions")) return citations;

		const auto parents = componentParents(document);
		for (const auto& j : document.at("sourceDescriptions")) {
			const std::string id = j.value("id", "");
			if (!parents.count(id)) continue;

			Citation c;
			c.id = id;
			c.sourceId = rootSourceId(parents, id);
			if (c.sourceId.empty()) continue;  // componentOf loop: garbage in, skipped

			if (j.contains("citations") && !j.at("citations").empty())
				c.page = j.at("citations")[0].value("value", "");
			if (j.contains("coverage") && !j.at("coverage").empty() && j.at("coverage")[0].contains("temporal")) {
				const auto& temporal = j.at("coverage")[0].at("temporal");
				c.date = parseDate(temporal);
			}
			c.confidence = j.value("confidence", "");
			c.attributes = parseAttributes(j);
			c.notes = parseNotes(j);
			citations.push_back(std::move(c));
		}

		return citations;
	}

	// Parsing one object at a time can't tell "#X" the source from "#X" the citation. Once the
	// whole document is parsed, point references that name a citation at that citation.
	void resolveCitationReferences(std::vector<Person>& persons, std::vector<Relationship>& relationships, std::vector<Event>& events, const std::vector<Citation>& citations) {
		std::unordered_set<std::string> ids;
		for (const auto& c : citations) ids.insert(c.id);
		if (ids.empty()) return;

		for (auto& p : persons) {
			remapRefs(p.conclusion.sources, ids);
			for (auto& f : p.facts) remapRefs(f.conclusion.sources, ids);
		}
		for (auto& r : relationships) {
			remapRefs(r.conclusion.sources, ids);
			for (auto& f : r.facts) remapRefs(f.conclusion.sources, ids);
		}
		for (auto& e : events) {
			remapRefs(e.conclusion.sources, ids);
			for (auto& role : e.roles) remapRefs(role.conclusion.sources, ids);
		}
	}

	void resolveCitationReferences(std::vector<Person>& persons, std::vector<Relationship>& relationships, const std::vector<Citation>& citations) {
		std::vector<Event> none;
		resolveCitationReferences(persons, relationships, none, citations);
	}

	// --- Serializers ---

	nlohmann::json serializeConclusion(const gedcomx::Conclusion& conclusion) {
		nlohmann::json j;
		if (!conclusion.confidence.empty()) j["confidence"] = conclusion.confidence;
		if (!conclusion.lang.empty()) j["lang"] = conclusion.lang;

		if (!conclusion.sources.empty()) {
			nlohmann::json sources = nlohmann::json::array();
			for (const auto& src : conclusion.sources) {
				const std::string target = gedcomx::bareCitationSourceId(src.citationId).value_or(src.citationId);
				sources.push_back({ {"description", {{"resource", "#" + target}}} });
			}
			j["sources"] = sources;
		}

		if (!conclusion.notes.empty()) j["notes"] = serializeNotes(conclusion.notes);

		return j;
	}

	nlohmann::json serializeFact(const gedcomx::Fact& fact) {
		nlohmann::json j = serializeConclusion(fact.conclusion);
		j["id"] = fact.id;
		j["type"] = fact.type;

		if (!isEmptyDate(fact.date)) j["date"] = serializeDate(fact.date);

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
				nlohmann::json nameJson;
				if (!name.title.empty()) nameJson["title"] = name.title;
				if (!name.nickName.empty()) nameJson["nickName"] = name.nickName;
				if (!name.callName.empty()) nameJson["callName"] = name.callName;
				nameJson["nameForms"] = nameForms;
				names.push_back(nameJson);
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

		if (person.gender && !person.gender->type.empty()) j["gender"]["type"] = person.gender->type;

		if (!person.attributes.empty()) j["attributes"] = serializeAttributes(person.attributes);

		if (!person.addresses.empty()) {
			nlohmann::json addrs = nlohmann::json::array();
			for (const auto& a : person.addresses) addrs.push_back(serializeAddress(a));
			j["addresses"] = addrs;
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

	nlohmann::json serializeEventRole(const gedcomx::EventRole& role) {
		nlohmann::json j = serializeConclusion(role.conclusion);
		j["person"] = { { "resource", "#" + role.personId } };
		if (!role.type.empty()) j["type"] = role.type;
		if (!role.details.empty()) j["details"] = role.details;
		return j;
	}

	nlohmann::json serializeEvent(const gedcomx::Event& event) {
		nlohmann::json j = serializeConclusion(event.conclusion);
		j["id"] = event.id;
		if (!event.type.empty()) j["type"] = event.type;
		if (!isEmptyDate(event.date)) j["date"] = serializeDate(event.date);

		if (!event.place.original.empty() || !event.place.descriptionId.empty()) {
			nlohmann::json place;
			if (!event.place.original.empty()) place["original"] = event.place.original;
			if (!event.place.descriptionId.empty()) place["description"] = { { "resource", "#" + event.place.descriptionId } };
			j["place"] = place;
		}

		if (!event.description.empty()) j["description"] = event.description;

		if (!event.roles.empty()) {
			nlohmann::json roles = nlohmann::json::array();
			for (const auto& r : event.roles) roles.push_back(serializeEventRole(r));
			j["roles"] = roles;
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

		// author, publication and abbreviation are not GEDCOM X properties: app extensions.
		// (GEDCOM X's "authors" and "publisher" are Agent references, so plain text has no standard home.)
		if (!source.author.empty()) j["author"] = source.author;
		if (!source.publication.empty()) j["publication"] = source.publication;
		if (!source.abbreviation.empty()) j["abbreviation"] = source.abbreviation;
		if (!source.notes.empty()) j["notes"] = serializeNotes(source.notes);
		if (!source.attributes.empty()) j["attributes"] = serializeAttributes(source.attributes);

		return j;
	}

	nlohmann::json serializeSourceExport(const gedcomx::SourceDescription& source,
		const std::vector<gedcomx::Person>& persons,
		const std::vector<gedcomx::Relationship>& relationships,
		const std::vector<gedcomx::SourceDescription>& otherSources,
		const std::vector<gedcomx::Citation>& citations,
		const std::vector<gedcomx::Event>& events) {
		nlohmann::json doc;

		doc["sourceDescriptions"] = nlohmann::json::array({ serializeSourceDescription(source) });
		for (const auto& s : otherSources) doc["sourceDescriptions"].push_back(serializeSourceDescription(s));
		for (const auto& c : citations) {
			if (isBare(c)) continue;  // a bare citation is just "#S1" on the refeence; it has no description
			doc["sourceDescriptions"].push_back(serializeCitation(c));
		}

		nlohmann::json personsJson = nlohmann::json::array();
		for (const auto& p : persons) personsJson.push_back(serializePerson(p));
		doc["persons"] = personsJson;

		nlohmann::json relsJson = nlohmann::json::array();
		for (const auto& r : relationships) relsJson.push_back(serializeRelationship(r));
		doc["relationships"] = relsJson;

		if (!events.empty()) {
			nlohmann::json eventsJson = nlohmann::json::array();
			for (const auto& e : events)
				eventsJson.push_back(serializeEvent(e));
			doc["events"] = eventsJson;
		}

		return doc;
	}

	nlohmann::json serializeCitation(const Citation& c) {
		nlohmann::json j;
		j["id"] = c.id;
		j["componentOf"]["description"] = "#" + c.sourceId;

		if (!c.page.empty()) {
			nlohmann::json citation;
			citation["value"] = c.page;
			j["citations"] = nlohmann::json::array({ citation });
		}
		if (!isEmptyDate(c.date)) {
			nlohmann::json coverage;
			coverage["temporal"] = serializeDate(c.date);
			j["coverage"] = nlohmann::json::array({ coverage });
		}

		if (!c.confidence.empty()) j["confidence"] = c.confidence;  // not a GEDCOM X property; an app extension
		if (!c.attributes.empty()) j["attributes"] = serializeAttributes(c.attributes);
		if (!c.notes.empty()) j["notes"] = serializeNotes(c.notes);

		return j;
	}
}