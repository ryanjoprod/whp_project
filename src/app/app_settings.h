#pragma once

#include <string>

#include <nlohmann/json.hpp>

struct AppSettings {
	float repulsionStrength = 8000.0f;
	float springStrength = 0.02f;
	float restLength = 150.0f;
	float damping = 0.85f;
	float centeringStrength = 0.01f;
	float lodZoomThreshold = 0.4f;
};

std::string serializeAppSettings(const AppSettings& settings);
AppSettings deserializeAppSettings(const std::string& jsonText);