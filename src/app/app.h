#pragma once

#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <set>

#include "window.h"
#include "project.h"
#include "storage/database.h"
#include "gedcomx/person.h"

class App {
public:
	explicit App(const AppSettings& settings);
	void run();

private:
	std::string loadSourceFile(const std::string& filePath);

	std::optional<Project> currentProject_;
	std::unique_ptr<storage::Database> db_;	 // null until a project is open
	Window window_;

	void newProject(const std::string& folderPath, const std::string& projectName);
	void openProject(const std::string& whprojPath);
	void saveProject();
	void closeCurrentProject();

	std::string projectFilePath() const;  // helper: projectFolder + "/" + projectName + ".whproj"

	size_t maxVisibleNodes_ = 200;  // this is how many people are visible on the screen at once

	std::unordered_map<std::string, gedcomx::Person> visiblePeopleMap_;
	std::unordered_map<std::string, gedcomx::Relationship> visibleRelationshipsMap_;

	std::vector<gedcomx::Person> visiblePeople_;
	std::vector<gedcomx::Relationship> visibleRelationships_;

	std::string currentRootKey_;
	int neighborhoodDepth_ = 2;
	void loadNeighborhood(const std::string& rootPersonKey, int depth);

	void rebuildVisibleVectors();
	void evictFarthestUntilUnderCap();

	std::vector<gedcomx::SourceDescription> knownSources_;

	std::string pendingExportSourceId_;

	// Aggregate layer
	bool aggregateViewDirty_ = true;
	std::vector<gedcomx::Person> aggregateVisiblePeople_;
	std::vector<gedcomx::Relationship> aggregateVisibleRelationships_;
	void rebuildAggregateView();
};