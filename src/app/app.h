#pragma once

#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <set>

#include "window.h"
#include "window_state.h"
#include "project.h"

#include "storage/database.h"
#include "gedcomx/person.h"

class App {
public:
	explicit App(const WindowState& windowState);
	void run();

private:
	std::string loadSourceFile(const std::string& filePath);

	std::optional<Project> currentProject_;
	std::unique_ptr<storage::Database> db_;	 // null until a project is open
	Window window_;

	void renderFrame();

	void newProject(const std::string& folderPath, const std::string& projectName);
	void openProject(const std::string& whprojPath);
	void saveProject();
	void backUpProject(const std::string& destinationFolder);
	void closeCurrentProject();

	std::string projectFilePath() const;  // helper: projectFolder + "/" + projectName + ".whproj"
	std::string physicsSettingsPath() const;  // in the project folder; empty with no project
	void loadPhysicsSettings();

	size_t maxVisibleNodes_ = 200;  // this is how many people are visible on the screen at once

	std::unordered_map<std::string, gedcomx::Person> visiblePeopleMap_;
	std::unordered_map<std::string, gedcomx::Relationship> visibleRelationshipsMap_;

	std::vector<gedcomx::Person> visiblePeople_;
	std::vector<gedcomx::Relationship> visibleRelationships_;

	std::string currentRootKey_;
	int neighborhoodDepth_ = 2;
	void loadNeighborhood(const std::string& rootPersonKey, int depth);
	void reloadNeighborhoodFresh();  // drop what's on screen, then load around the current root
	void pushPersonEvents(const std::string& personRowKey);

	void rebuildVisibleVectors();
	void evictFarthestUntilUnderCap();

	std::vector<gedcomx::SourceDescription> knownSources_;
	std::vector<gedcomx::Citation> knownCitations_;
	void refreshKnownCitations();

	void refreshKnownPlaces();

	std::string pendingExportSourceId_;

	// Aggregate layer
	bool aggregateViewDirty_ = true;
	std::vector<gedcomx::Person> aggregateVisiblePeople_;
	std::unordered_map<std::string, std::vector<std::string>> aggregateContributingSourceIds_; // merged person id -> distinct source ids
	std::vector<gedcomx::Relationship> aggregateVisibleRelationships_;
	void rebuildAggregateView();

	void pushSourceEvents(const std::string& sourceId);
	std::unordered_map<std::string, std::string> namesForEvents(const std::vector<gedcomx::Event>& events);
	void pushDetailsEvents(const std::string& personRowKey);
};