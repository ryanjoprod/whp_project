#pragma once

namespace storage {
	// Database schema version
	constexpr int kSchemaVersion = 3;

	constexpr const char* kSchemaSql = R"(
		CREATE TABLE IF NOT EXISTS sources (
			id TEXT PRIMARY KEY,
			title TEXT,
			citation TEXT
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

		CREATE TABLE IF NOT EXISTS person_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(row_key) ON DELETE CASCADE
		); 	 

		CREATE TABLE IF NOT EXISTS person_facts (
			row_key TEXT PRIMARY KEY,
			local_id TEXT,
			person_id TEXT NOT NULL,
			type TEXT,
			date_original TEXT,
			date_formal TEXT,
			place_original TEXT,	
			place_description_id TEXT,
			value TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(row_key)	ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS person_fact_sources (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_fact_id TEXT NOT NULL,
			source_description_id TEXT NOT NULL,
			FOREIGN KEY (person_fact_id) REFERENCES person_facts(row_key) ON DELETE CASCADE,
			FOREIGN KEY (source_description_id) REFERENCES sources(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS person_fact_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_fact_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (person_fact_id) REFERENCES person_facts(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS person_links (
			aggregate_id TEXT NOT NULL,
			person_id TEXT NOT NULL,
			FOREIGN KEY (person_id) REFERENCES persons(row_key)	ON DELETE CASCADE
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
			place_original TEXT,
			place_description_id TEXT,
			value TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (relationship_id) REFERENCES relationships(row_key)	ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS relationship_sources (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			relationship_id TEXT NOT NULL,
			source_description_id TEXT NOT NULL,
			FOREIGN KEY (relationship_id) REFERENCES relationships(row_key) ON DELETE CASCADE,
			FOREIGN KEY (source_description_id) REFERENCES sources(id) ON DELETE RESTRICT
		);

		CREATE TABLE IF NOT EXISTS relationship_fact_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			relationship_fact_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (relationship_fact_id) REFERENCES relationship_facts(row_key) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS relationship_fact_sources (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			relationship_fact_id TEXT NOT NULL,
			source_description_id TEXT NOT NULL,
			FOREIGN KEY (relationship_fact_id) REFERENCES relationship_facts(row_key) ON DELETE CASCADE,
			FOREIGN KEY (source_description_id) REFERENCES sources(id) ON DELETE RESTRICT
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
			local_id TEXT,
			type TEXT,
			date_original TEXT,
			date_formal TEXT,
			place_original TEXT,
			confidence TEXT,
			lang TEXT
		);

		CREATE TABLE IF NOT EXISTS event_roles (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			event_id TEXT NOT NULL,
			type TEXT,
			person_id TEXT,
			details TEXT,
			confidence TEXT,
			lang TEXT,
			FOREIGN KEY (event_id) REFERENCES events(row_key) ON DELETE CASCADE
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

		CREATE TABLE IF NOT EXISTS documents (
			id TEXT PRIMARY KEY,
			type TEXT,
			text_type TEXT,
			extracted TEXT,
			text TEXT,
			confidence TEXT,
			lang TEXT
		);

		CREATE TABLE IF NOT EXISTS document_notes (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			document_id TEXT NOT NULL,
			lang TEXT,
			subject TEXT,
			text TEXT,
			FOREIGN KEY (document_id) REFERENCES documents(id) ON DELETE CASCADE
		);

		CREATE TABLE IF NOT EXISTS places (
			id TEXT PRIMARY KEY,
			type TEXT,
			latitude TEXT,
			longitude TEXT,
			jurisdiction_id TEXT,
			confidence TEXT,
			lang TEXT
		);

		CREATE TABLE IF NOT EXISTS place_names (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			place_id TEXT NOT NULL,
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
	)";
}