#include "app_settings.h"
#include "logger.h"

std::string serializeAppSettings(const AppSettings& settings) {
	nlohmann::json j;

	j["repulsionStrength"] = settings.repulsionStrength;
	j["springStrength"] = settings.springStrength;
	j["restLength"] = settings.restLength;
	j["damping"] = settings.damping;
	j["centeringStrength"] = settings.centeringStrength;
	j["lodZoomThreshold"] = settings.lodZoomThreshold;

	return j.dump(2);
}

AppSettings deserializeAppSettings(const std::string& jsonText) {
	AppSettings settings;  // defaults, used if parsing fails or fields are missing

	try {
		nlohmann::json j = nlohmann::json::parse(jsonText);

		settings.repulsionStrength = j.value("repulsionStrength", settings.repulsionStrength);
		settings.springStrength = j.value("springStrength", settings.springStrength);
		settings.restLength = j.value("restLength", settings.restLength);
		settings.damping = j.value("damping", settings.damping);
		settings.centeringStrength = j.value("centeringStrength", settings.centeringStrength);
		settings.lodZoomThreshold = j.value("lodZoomThreshold", settings.lodZoomThreshold);
	}
	catch (const std::exception& e) {
		Logger::log(LogLevel::Warning, std::string("Failed to parse app settings, using defaults: ") + e.what());
	}

	return settings;
}