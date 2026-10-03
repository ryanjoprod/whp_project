#pragma once

#include <string>

#include "gedcomx/conclusion.h"

namespace gedcomx {
	struct Document {
		std::string id;
		std::string type;
		std::string textType;
		bool extracted = false;
		std::string text;
		Conclusion conclusion;
	};
}