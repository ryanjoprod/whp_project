#pragma once

namespace storage {
	// Database schema version
	constexpr int kSchemaVersion = 8;

	constexpr const char* kSchemaSql = R"(
		CREATE TABLE IF NOT EXISTS sources (
			id TEXT PRIMARY KEY,
			title TEXT,
			citation TEXT,
			author TEXT,
			publication TEXT,
			abbreviation TEXT
		);

		CREATE TABLE IF NOT EXISTS source_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			source_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS source_attributes (
			row_key TEXT PRIMARY KEY,
			source_id TEXT NOT NULL,
			type TEXT,
			value TEXT,
			FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS citations (
			id TEXT PRIMARY KEY,
			source_id TEXT NOT NULL,
			page TEXT,
			date_original TEXT,
			date_formal TEXT,
			date_calendar TEXT,
			date_quality TEXT,
			date_sort INTEGER,
			confidence TEXT,
			FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS citation_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			citation_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS citation_attributes (
			row_key TEXT PRIMARY KEY,
			citation_id TEXT NOT NULL,
			type TEXT,
			value TEXT,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS persons (
			row_key TEXT PRIMARY KEY,
			local_id TEXT NOT NULL,
			source_id TEXT NOT NULL,
			confidence TEXT,
			lang TEXT,
			gender TEXT,
			FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS person_links (
			person_id TEXT PRIMARY KEY,
			aggregate_id TEXT NOT NULL,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS person_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE
		); 

		CREATE TABLE IF NOT EXISTS person_citations (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_id TEXT NOT NULL,
			citation_id TEXT NOT NULL,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE RESTRICT
		);	 

		CREATE TABLE IF NOT EXISTS person_facts (
			row_key TEXT PRIMARY KEY,
			local_id TEXT,
			person_id TEXT NOT NULL,
			type TEXT,
			date_original TEXT,
			date_formal TEXT,
			date_calendar TEXT,
			date_quality TEXT,
			date_sort INTEGER,
			place_original TEXT,	
			place_description_id TEXT,
			value TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(row_key)	ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS person_fact_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_fact_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (person_fact_id) REFERENCES person_facts(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS person_fact_citations (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_fact_id TEXT NOT NULL,
			citation_id TEXT NOT NULL,
			FOREIGN KEY (person_fact_id) REFERENCES person_facts(row_key) ON DELETE CASCADE,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS person_fact_attributes (
			row_key TEXT PRIMARY KEY,
			person_fact_id TEXT NOT NULL,
			type TEXT,
			value TEXT,
			FOREIGN KEY (person_fact_id) REFERENCES person_facts(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS person_attributes (
			row_key TEXT PRIMARY KEY,
			person_id TEXT NOT NULL,
			type TEXT,
			value TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS person_addresses (
			row_key TEXT PRIMARY KEY,
			person_id TEXT NOT NULL,
			street TEXT,
			city TEXT,
			state_or_province TEXT,
			country TEXT,
			postal_code TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS names (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_id TEXT NOT NULL,
			title TEXT,
			nick_name TEXT,
			call_name TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS name_forms (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			name_id INTEGER NOT NULL,
			full_text TEXT,
			FOREIGN KEY (name_id) REFERENCES names(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS name_parts (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			name_form_id INTEGER NOT NULL,
			value TEXT NOT NULL,
			type TEXT,
			FOREIGN KEY (name_form_id) REFERENCES name_forms(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS relationships (
			row_key TEXT PRIMARY KEY,
			local_id TEXT NOT NULL,
			source_id TEXT NOT NULL,
			type TEXT NOT NULL,
			person1_id TEXT NOT NULL,
			person2_id TEXT NOT NULL,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE RESTRICT,
			FOREIGN KEY (person1_id) REFERENCES persons(row_key) ON DELETE CASCADE,
			FOREIGN KEY (person2_id) REFERENCES persons(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS relationship_citations (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			relationship_id TEXT NOT NULL,
			citation_id TEXT NOT NULL,
			FOREIGN KEY (relationship_id) REFERENCES relationships(row_key) ON DELETE CASCADE,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS relationship_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			relationship_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (relationship_id) REFERENCES relationships(row_key)	ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS relationship_facts (
			row_key TEXT PRIMARY KEY,
			local_id TEXT,
			relationship_id TEXT NOT NULL,
			type TEXT,
			date_original TEXT,
			date_formal TEXT,
			date_calendar TEXT,
			date_quality TEXT,
			date_sort INTEGER,
			place_original TEXT,
			place_description_id TEXT,
			value TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (relationship_id) REFERENCES relationships(row_key)	ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS relationship_fact_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			relationship_fact_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (relationship_fact_id) REFERENCES relationship_facts(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS relationship_fact_citations (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			relationship_fact_id TEXT NOT NULL,
			citation_id TEXT NOT NULL,
			FOREIGN KEY (relationship_fact_id) REFERENCES relationship_facts(row_key) ON DELETE CASCADE,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS relationship_fact_attributes (
			row_key TEXT PRIMARY KEY,
			relationship_fact_id TEXT NOT NULL,
			type TEXT,
			value TEXT,
			FOREIGN KEY (relationship_fact_id) REFERENCES relationship_facts(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS agents (
			id TEXT PRIMARY KEY
		);

		CREATE TABLE IF NOT EXISTS agent_names (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			agent_id TEXT NOT NULL,
			lang TEXT,
			value TEXT,
			FOREIGN KEY (agent_id) REFERENCES agents(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS agent_emails (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			agent_id TEXT NOT NULL,
			email TEXT,
			FOREIGN KEY (agent_id) REFERENCES agents(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS agent_phones (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			agent_id TEXT NOT NULL,
			phone TEXT,
			FOREIGN KEY (agent_id) REFERENCES agents(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS agent_addresses (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			agent_id TEXT NOT NULL,
			street TEXT,
			city TEXT,
			state_or_province TEXT,
			postal_code TEXT,
			country TEXT,
			FOREIGN KEY (agent_id) REFERENCES agents(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS events (
			row_key TEXT PRIMARY KEY,
			local_id TEXT NOT NULL,
			source_id TEXT NOT NULL,
			type TEXT,
			date_original TEXT,
			date_formal TEXT,
			date_calendar TEXT,
			date_quality TEXT,
			date_sort INTEGER,
			place_original TEXT,
			place_description_id TEXT,
			description TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS event_citations (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			event_id TEXT NOT NULL,
			citation_id TEXT NOT NULL,
			FOREIGN KEY (event_id) REFERENCES events(row_key) ON DELETE CASCADE,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS event_roles (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			event_id TEXT NOT NULL,
			type TEXT,
			person_id TEXT NOT NULL,
			details TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (event_id) REFERENCES events(row_key) ON DELETE CASCADE,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS event_role_citations (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			event_role_id INTEGER NOT NULL,
			citation_id TEXT NOT NULL,
			FOREIGN KEY (event_role_id) REFERENCES event_roles(id) ON DELETE CASCADE,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS event_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			event_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (event_id) REFERENCES events(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS event_role_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			event_role_id INTEGER NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (event_role_id) REFERENCES event_roles(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS event_attributes (
			row_key TEXT PRIMARY KEY,
			event_id TEXT NOT NULL,
			type TEXT,
			value TEXT,
			FOREIGN KEY (event_id) REFERENCES events(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS event_role_attributes (
			row_key TEXT PRIMARY KEY,
			event_role_id INTEGER NOT NULL,
			type TEXT,
			value TEXT,
			FOREIGN KEY (event_role_id) REFERENCES event_roles(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS documents (
			row_key TEXT PRIMARY KEY,
			local_id TEXT NOT NULL,
			source_id TEXT NOT NULL,
			type TEXT,
			text_type TEXT,
			extracted INTEGER,
			text TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (source_id) REFERENCES sources(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS document_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			document_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (document_id) REFERENCES documents(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS places (
			id TEXT PRIMARY KEY,
			type TEXT,
			latitude REAL,
			longitude REAL,
			code TEXT,
			confidence TEXT,
			lang TEXT
		);

		CREATE TABLE IF NOT EXISTS place_names (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			place_id TEXT NOT NULL,
			date_original TEXT,
			date_formal TEXT,
			date_calendar TEXT,
			date_quality TEXT,
			date_sort INTEGER,
			lang TEXT,
			value TEXT,
			FOREIGN KEY (place_id) REFERENCES places(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS place_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			place_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (place_id) REFERENCES places(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS place_jurisdictions (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			place_id TEXT NOT NULL,
			parent_id TEXT NOT NULL,
			date_original TEXT,
			date_formal TEXT,
			date_calendar TEXT,
			date_quality TEXT,
			date_sort INTEGER,
			FOREIGN KEY (place_id) REFERENCES places(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
			FOREIGN KEY (parent_id) REFERENCES places(id) ON DELETE RESTRICT DEFERRABLE INITIALLY DEFERRED
		);

		CREATE TABLE IF NOT EXISTS place_citations (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			place_id TEXT NOT NULL,
			citation_id TEXT NOT NULL,
			FOREIGN KEY (place_id) REFERENCES places(id) ON DELETE CASCADE,
			FOREIGN KEY (citation_id) REFERENCES citations(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS place_urls (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			place_id TEXT NOT NULL,
			url TEXT,
			description TEXT,
			type TEXT,
			FOREIGN KEY (place_id) REFERENCES places(id) ON DELETE CASCADE
		);
	)";
}