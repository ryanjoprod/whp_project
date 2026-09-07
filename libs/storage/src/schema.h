#pragma once

namespace storage {
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
			FOREIGN KEY (source_id) REFERENCES sources(id)
		);

		CREATE TABLE IF NOT EXISTS names (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			person_id TEXT NOT NULL,
			FOREIGN KEY (person_id) REFERENCES persons(id)
		);

		CREATE TABLE IF NOT EXISTS name_forms (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			name_id INTEGER NOT NULL,
			full_text TEXT,
			FOREIGN KEY (name_id) REFERENCES names(id)
		);

		CREATE TABLE IF NOT EXISTS name_parts (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			name_form_id INTEGER NOT NULL,
			value TEXT NOT NULL,
			type TEXT,
			FOREIGN KEY (name_form_id) REFERENCES name_forms(id)
		);	 

		CREATE TABLE IF NOT EXISTS person_facts (
			row_id TEXT PRIMARY KEY,
			local_id TEXT,
			person_id TEXT NOT NULL,
			type TEXT,
			date_original TEXT,
			date_formal TEXT,
			place_original TEXT,
			value TEXT,
			FOREIGN KEY (person_id) REFERENCES persons(id)
		);

		CREATE TABLE IF NOT EXISTS relationships (
			row_key TEXT PRIMARY KEY,
			local_id TEXT NOT NULL,
			source_id TEXT NOT NULL,
			type TEXT NOT NULL,
			person1_id TEXT NOT NULL,
			person2_id TEXT NOT NULL,
			FOREIGN KEY (person1_id) REFERENCES persons(id),
			FOREIGN KEY (person2_id) REFERENCES persons(id)
		);

		CREATE TABLE IF NOT EXISTS relationship_facts (
			row_id TEXT PRIMARY KEY,
			local_id TEXT,
			relationship_id TEXT NOT NULL,
			type TEXT,
			date_original TEXT,
			date_formal TEXT,
			place_original TEXT,
			value TEXT,
			FOREIGN KEY (relationship_id) REFERENCES relationships(id)
		);

		CREATE TABLE IF NOT EXISTS person_links (
			aggregate_id TEXT NOT NULL,
			person_id TEXT NOT NULL,
			FOREIGN KEY (person_id) REFERENCES persons(row_key)
		);
	)";
}