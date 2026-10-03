#include <random>
#include <sstream>

#include "id_generator.h"

namespace idgen {
	std::string generateLocalId(const std::string& prefix) {
		static std::random_device rd;
		static std::mt19937_64 rng(rd());
		std::uniform_int_distribution<uint64_t> dist;

		std::ostringstream oss;
		oss << prefix << std::hex << dist(rng);

		return oss.str();
	}
}