#pragma once

#include <string>
#include <vector>

#include "gedcomx/text_value.h"

namespace gedcomx {
	struct Address
	{
		std::string id;
		std::string street;
		std::string city;
		std::string stateOrProvince;
		std::string postalCode;
		std::string country;
	};

	struct Agent {
		std::string id;
		std::vector<TextValue> names;
		std::vector<std::string> emails;
		std::vector<std::string> phones;
		std::vector<Address> addresses;

		std::string primaryName() const {
			return names.empty() ? "" : names[0].value;
		}
	};
}