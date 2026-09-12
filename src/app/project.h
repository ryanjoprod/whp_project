#pragma once

#include <string>
#include <vector>
#include <optional>

#include <nlohmann/json.hpp>

struct ProjectSettings {
	int neighborhoodDepth = 2;
	size_t maxVisibleNodes = 200;
};

struct Project {
	std::string projectName;
	std::string dbFile;  // relative to project folder
	std::vector<std::string> sourceFiles;  // relatie paths, e.g. "sources/foo.json"

	std::string currentRootKey;
	float cameraOffsetX = 0.0f;
	float cameraOffsetY = 0.0f;
	float cameraZoom = 1.0f;

	ProjectSettings settings;

	// The folder this project lives in - not serialized into the .whproj
	// itself (it's implied by wherever the file was opened from), but kept
	// here at runtime since almost everything else needs it (db path,
	// source file paths, etc. are all relative to this folder).
	std::string projectFolder;
};

// Serializes to / parses from the .whproj JSON format below.
// Returns std::nullopt on parse failure.
// {
//   "projectName": "MyProject",
//   "dbFile" : "MyProject.db",
//   "sourceFiles" : ["sources/sample_person.json"] ,
//   "currentRootKey" : "S1:P1",
//   "cameraOffset" : { "x": 12.4, "y" : -8.1 },
//   "cameraZoom" : 1.2,
//   "settings" : {
//     "neighborhoodDepth": 2,
//     "maxVisibleNodes" : 200
//   }
// }
std::string serializeProject(const Project& project);
std::optional<Project> deserializeProject(const std::string& jsonText, const std::string& projectFolder);