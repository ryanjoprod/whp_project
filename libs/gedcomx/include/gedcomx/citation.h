#pragma once

#include <string>
#include <vector>

#include "gedcomx/conclusion.h"  // Note
#include "gedcomx/fact.h"  // Date
#include "gedcomx/attribute.h"  // Attribute

namespace gedcomx {
	// A specific place within a Source: "Volume/Page" in Gramps terms. Shared by
	// everything that cites it. Exported to GEDCOM X as a child SourceDescription
	// (componentOf -> the source) - that mapping arrives in a later step
	struct Citation {
		std::string id;  // local id, e.g. "C_1a2b..."
		std::string sourceId;  // the Source (SourceDescription) this belongs to
		std::string page;  // volume / page / entry text
		Date date;  // same original + formal pair the facts use
		std::string confidence;  // app-specific; free text here, the UI will pick the values
		std::vector<Note> notes;
		std::vector<Attribute> attributes;
	};

	// A "bare" citation means "this cites the source as a whole". One per source, created on
	// demand, with an id derived from the source so it can never collide with generated ids.
	inline std::string bareCitationId(const std::string& sourceId) {
		return "CB_" + sourceId;
	}
	inline bool isBare(const Citation& c) {
		return c.id == bareCitationId(c.sourceId);
	}

	// Inverse of bareCitationId: the source id if this is a bare citation id, otherwise nothing.
	inline std::optional<std::string> bareCitationSourceId(const std::string& citationId) {
		if (citationId.rfind("CB_", 0) == 0) return citationId.substr(3);
		return std::nullopt;
	}

	// True when a citation has no details of its own. Saving one of these cites the whole source instead.
	inline bool isBlankCitation(const Citation& c) {
		return c.page.empty()
			&& c.date.original.empty()
			&& c.date.formal.empty()
			&& c.confidence.empty()
			&& c.notes.empty()
			&& c.attributes.empty();
	}

	// Short text for lists: the page if these is one, otherwise the date.
	inline std::string citationSummary(const Citation& c) {
		if (isBare(c)) return "whole source";
		if (!c.page.empty()) return c.page;
		if (!c.date.original.empty()) return c.date.original;
		return "(no details)";
	}
}