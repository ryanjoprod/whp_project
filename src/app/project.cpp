#include <iostream>

#include "project.h"

std::string serializeProject(const Project& project) {
	nlohmann::json j;
	j["projectName"] = project.projectName;
	j["dbFile"] = project.dbFile;
	j["sourceFiles"] = project.sourceFiles;
	j["currentRootKey"] = project.currentRootKey;
	j["cameraOffset"] = { {"x", project.cameraOffsetX},{"y",project.cameraOffsetY} };
	j["cameraZoom"] = project.cameraZoom;
	j["settings"] = {
		{"neighborhoodDepth", project.settings.neighborhoodDepth},
		{"maxVisibleNodes", project.settings.maxVisibleNodes}
	};

	return j.dump(2);  // pretty-printed, 2-space indent
}

std::optional<Project> deserializeProject(const std::string& jsonText, const std::string& projectFolder) {
	try {
		nlohmann::json j = nlohmann::json::parse(jsonText);

		Project project;
		project.projectFolder = projectFolder;
		project.projectName = j.value("projectName", "");
		project.dbFile = j.value("dbFile", "");
		project.sourceFiles = j.value("sourceFiles", std::vector<std::string>{});
		project.currentRootKey = j.value("currentRootKey", "");

		if (j.contains("cameraOffset")) {
			project.cameraOffsetX = j.at("cameraOffset").value("x", 0.0f);
			project.cameraOffsetY = j.at("cameraOffset").value("y", 0.0f);
		}
		project.cameraZoom = j.value("cameraZoom", 1.0f);

		if (j.contains("settings")) {
			project.settings.neighborhoodDepth = j.at("settings").value("neighborhoodDepth", 2);
			project.settings.maxVisibleNodes = j.at("settings").value("maxVisibleNodes", (size_t)200);
		}

		return project;
	}
	catch (const std::exception& e) {
		std::cerr << "Failed to parse file: " << e.what() << "\n";
		return std::nullopt;
	}
}