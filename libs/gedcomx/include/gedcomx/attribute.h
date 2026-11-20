#pragma once

#include <string>

namespace gedcomx {
	struct Attribute {  // new type, not in GEDCOM X core
		std::string id;
		std::string type;  // free text, e.g. "Occupation", "Cause of Death"
		std::string value;
	};
}