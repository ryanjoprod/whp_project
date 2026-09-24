#include <filesystem>
#include <fstream>
#include <queue>
#include <unordered_set>
#include <limits>
#include <algorithm>
#include <iostream>

#include "app.h"
#include "logger.h"
#include "app_settings.h"

#include "gedcomx/parser.h"

namespace fs = std::filesystem;

App::App(const AppSettings& settings) : window_() {
	window_.setPhysicsSettings(settings);
}

std::string App::projectFilePath() const {
	if (!currentProject_) return "";
	return currentProject_->projectFolder + "/" + currentProject_->projectName + ".whproj";
}

void App::newProject(const std::string& folderPath, const std::string& projectName) {
	closeCurrentProject();

	std::string projectFolder = folderPath + "/" + projectName;
	fs::create_directories(projectFolder);
	fs::create_directories(projectFolder + "/sources");

	Project project;
	project.projectFolder = projectFolder;
	project.projectName = projectName;
	project.dbFile = projectName + ".db";

	db_ = std::make_unique<storage::Database>(projectFolder + "/" + project.dbFile);
	currentProject_ = project;

	saveProject();  // write the initial .whproj immediately
}

void App::openProject(const std::string& whoprojPath) {
	closeCurrentProject();

	std::ifstream file(whoprojPath);
	if (!file) {
		Logger::log(LogLevel::Error, "Failed to open project file: " + whoprojPath);
		return;
	}

	std::string jsonText((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::string projectFolder = fs::path(whoprojPath).parent_path().string();

	auto loaded = deserializeProject(jsonText, projectFolder);
	if (!loaded) {
		Logger::log(LogLevel::Error, "Failed to parse project file: " + whoprojPath);
		return;
	}

	currentProject_ = loaded;
	db_ = std::make_unique<storage::Database>(projectFolder + "/" + currentProject_->dbFile);

	knownSources_ = db_->loadAllSourceDescriptions();

	neighborhoodDepth_ = currentProject_->settings.neighborhoodDepth;
	maxVisibleNodes_ = currentProject_->settings.maxVisibleNodes;
	currentRootKey_ = currentProject_->currentRootKey;

	window_.setCameraState(currentProject_->cameraOffsetX, currentProject_->cameraOffsetY, currentProject_->cameraZoom);

	if (!currentRootKey_.empty()) {
		auto rootPerson = db_->loadPerson(currentRootKey_);
		if (rootPerson) {
			loadNeighborhood(currentRootKey_, neighborhoodDepth_);
		}
		else {
			Logger::log(LogLevel::Error, "Saved root person no longer exists; starting with an empty view.\n");
			currentRootKey_.clear();
		}
	}
}

void App::saveProject() {
	if (!currentProject_ || !db_) return;

	currentProject_->currentRootKey = currentRootKey_;

	ImVec2 offset = window_.cameraOffset();
	currentProject_->cameraOffsetX = offset.x;
	currentProject_->cameraOffsetY = offset.y;
	currentProject_->cameraZoom = window_.cameraZoom();

	currentProject_->settings.neighborhoodDepth = neighborhoodDepth_;
	currentProject_->settings.maxVisibleNodes = maxVisibleNodes_;

	std::ofstream out(projectFilePath());
	out << serializeProject(*currentProject_);
}

void App::closeCurrentProject() {
	db_.reset();  // closes the database connection
	currentProject_.reset();
	visiblePeopleMap_.clear();
	visibleRelationshipsMap_.clear();
	visiblePeople_.clear();
	visibleRelationships_.clear();
	currentRootKey_.clear();
	knownSources_.clear();
}

void App::loadNeighborhood(const std::string& rootPersonKey, int depth) {						

	std::unordered_set<std::string> visitedPersons;
	std::unordered_set<std::string> visitedRelationships;

	std::queue<std::pair<std::string, int>> toVisit;  // (personKey, currentDepth)
	toVisit.push({ rootPersonKey, 0 });
	visitedPersons.insert(rootPersonKey);

	while (!toVisit.empty()) {
		auto [personKey, currentDepth] = toVisit.front();
		toVisit.pop();

		auto person = db_->loadPerson(personKey);
		if (person) visiblePeopleMap_[personKey] = *person;  // union: insert-or-update, nothing removed here

		if (currentDepth >= depth) continue;  //stop expanding past the requested depth

		for (const auto& rel : db_->loadRelationshipsForPerson(personKey)) {
			if (visitedRelationships.insert(rel.id).second) {
				visibleRelationshipsMap_[rel.id] = rel;
			}

			// Whichever side isn't the person we came from is the neighbor to explore next.
			const std::string& neighborKey = (rel.person1Id == personKey) ? rel.person2Id : rel.person1Id;
			if (visitedPersons.insert(neighborKey).second) {
				toVisit.push({ neighborKey, currentDepth + 1 });
			}
		}
	}

	evictFarthestUntilUnderCap();
	rebuildVisibleVectors();
	window_.requestRefit();
}

std::string App::loadSourceFile(const std::string& filePath) {
	std::string pathToLoad = filePath;

	if (currentProject_) {
		// Copy the source into this project's sources/ folder, so the
		// project stays self-contained/portable.
		fs::path sourcePath(filePath);
		std::string destPath = currentProject_->projectFolder + "/sources/" + sourcePath.filename().string();

		try {
			if (fs::absolute(sourcePath) != fs::absolute(destPath)) {
				fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing);
			}
		}
		catch (const std::exception& e) {
			Logger::log(LogLevel::Error, std::string("Failed to copy source file into project: ") + e.what());
			return "";  // don't proceed with a half-copied/inconsistent state
		}

		std::string relativePath = "sources/" + sourcePath.filename().string();
		if (std::find(currentProject_->sourceFiles.begin(), currentProject_->sourceFiles.end(), relativePath) == currentProject_->sourceFiles.end()) {
			currentProject_->sourceFiles.push_back(relativePath);
		}

		pathToLoad = destPath;  // parse the copy, not the original
	}

	std::ifstream file(pathToLoad);
	if (!file) {
		Logger::log(LogLevel::Error, "Failed  to open file: " + pathToLoad);
		return "";
	};

	nlohmann::json document;
	file >> document;

	auto sourceDescriptions = gedcomx::parseSourceDescriptions(document);
	auto people = gedcomx::parsePersons(document);
	auto relationships = gedcomx::parseRelationships(document);
	auto agents = gedcomx::parseAgents(document);
	auto events = gedcomx::parseEvents(document);
	auto documents = gedcomx::parseDocuments(document);
	auto places = gedcomx::parsePlaceDescriptions(document);

	const std::string sourceId = sourceDescriptions.empty() ? "" : sourceDescriptions[0].id;

	for (const auto& p : places) db_->save(p);
	for (const auto& s : sourceDescriptions) db_->save(s);
	for (const auto& p : people) db_->save(p, sourceId);
	for (const auto& r : relationships) db_->save(r, sourceId);
	for (const auto& a : agents) db_->save(a);
	for (const auto& e : events) db_->save(e);
	for (const auto& d : documents) db_->save(d);

	// temporary test print code goes here

	// Return a re-rootable key for the first person in this file, it any,
	// so the caller can immediately navigate to what was just loaded.
	if (!people.empty()) {
		return sourceId + ":" + people[0].id;
	}

	return"";
}

void App::evictFarthestUntilUnderCap() {
	if (visiblePeopleMap_.size() <= maxVisibleNodes_) return;

	// BFS distance from the *current* root, over the union graph's edges -
	// this reuses the exact same traversal shape as loadNeighborhood itself,
	// just measuring "how far" instead of "which nodes."
	std::unordered_map<std::string, int> distance;
	std::queue<std::string> toVisit;
	toVisit.push(currentRootKey_);
	distance[currentRootKey_] = 0;

	while (!toVisit.empty()) {
		std::string current = toVisit.front();
		toVisit.pop();

		for (const auto& [id, rel] : visibleRelationshipsMap_) {
			if (rel.person1Id != current && rel.person2Id != current) continue;
			const std::string& neighbor = (rel.person1Id == current) ? rel.person2Id : rel.person1Id;
			if (distance.find(neighbor) == distance.end()) {
				distance[neighbor] = distance[current] + 1;
				toVisit.push(neighbor);
			}
		}
	}

	// Anyone not reached by that BFS is disconnected from the current root
	// entirely - treat them as "farthest" so they're evicted first.
	std::vector<std::string> ids;
	for (const auto& [id, person] : visiblePeopleMap_) ids.push_back(id);

	std::sort(ids.begin(), ids.end(),
		[&](const std::string& a, const std::string& b) {
			int da = distance.count(a) ? distance[a] : std::numeric_limits<int>::max();
			int db = distance.count(b) ? distance[b] : std::numeric_limits<int>::max();

			return da > db;  // farthest first
		});

	size_t toRemove = visiblePeopleMap_.size() - maxVisibleNodes_;
	for (size_t i = 0; i < toRemove; ++i) {
		visiblePeopleMap_.erase(ids[i]);
	}

	// Drop any relationship that now references a removed person.
	for (auto it = visibleRelationshipsMap_.begin(); it != visibleRelationshipsMap_.end(); ) {
		if (!visiblePeopleMap_.count(it->second.person1Id) || !visiblePeopleMap_.count(it->second.person2Id)) {
			it = visibleRelationshipsMap_.erase(it);
		}
		else {
			++it;
		}
	}
}

void App::rebuildVisibleVectors() {
	visiblePeople_.clear();
	for (const auto& [id, person] : visiblePeopleMap_) visiblePeople_.push_back(person);

	visibleRelationships_.clear();
	for (const auto& [id, rel] : visibleRelationshipsMap_) visibleRelationships_.push_back(rel);
}

void App::run() {
	while (window_.isRunning()) {
		window_.processEvents();
		window_.beginFrame();
		window_.renderTitleBarAndMenu();
		window_.renderCanvas(visiblePeople_, visibleRelationships_);
		window_.renderLogWindow();
		window_.renderSettingsPanel(neighborhoodDepth_, maxVisibleNodes_);
		window_.setProjectOpen(currentProject_.has_value());
		window_.renderCreationModals(knownSources_);
		window_.endFrame();

		auto newProjectReq = window_.consumeNewProjectRequest();
		if (newProjectReq) {
			auto folder = newProjectReq->first;
			auto name = newProjectReq->second;

			if (currentProject_) {
				// The confirm/switch itself can't throw - only the deferred
				// lambda's actual work can, so that's where the try/catch belongs
				window_.confirmProjectSwitch([this, folder, name]() {
					try {
						saveProject();
						newProject(folder, name);
					}
					catch (const std::exception& e) {
						Logger::log(LogLevel::Error, std::string("Failed to create project: ") + e.what());
					}
				});
			}
			else {
				try {
					newProject(folder, name);
				}
				catch (const std::exception& e) {
					Logger::log(LogLevel::Error, std::string("Failed to create project: ") + e.what());
				}
			}
		}

		auto openProjectReq = window_.consumeOpenProjectRequest();
		if (openProjectReq) {
			std::string path = *openProjectReq;
			if (currentProject_) {
				window_.confirmProjectSwitch([this, path]() {
					try {
						saveProject();
						openProject(path);
					}
					catch (const std::exception& e) {
						Logger::log(LogLevel::Error, std::string("Failed to open project: ") + e.what());
					}
				});
			}
			else {
				try {
					openProject(path);
				}
				catch (const std::exception& e) {
					Logger::log(LogLevel::Error, std::string("Failed to open project: ") + e.what());
				}
			}
		}

		if (auto newDepth = window_.consumeNeighborhoodDepthChange()) {
			neighborhoodDepth_ = *newDepth;
			if (!currentRootKey_.empty()) {
				loadNeighborhood(currentRootKey_, neighborhoodDepth_);	// re-explore at the new depth
			}
		}

		if (auto newMax = window_.consumeMaxVisibleNodesChange()) {
			maxVisibleNodes_ = *newMax;
			evictFarthestUntilUnderCap();  // apply the new cap immediately, in case it just got smaller
			rebuildVisibleVectors();
		}

		if (window_.consumeSaveProjectRequest()) {
			try {
				saveProject();
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to save project: ") + e.what());
			}
		}

		if (window_.consumeSaveSettingsRequest()) {
			std::ofstream out("app_settings.json");
			out << serializeAppSettings(window_.physicsSettings());
		}

		for (const auto& path : window_.consumeOpenedFiles()) {
			try {
				std::string newRoot = loadSourceFile(path);

				if (!newRoot.empty()) {
					currentRootKey_ = newRoot;
					loadNeighborhood(currentRootKey_, neighborhoodDepth_);
				}
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, "Failed to load source file '" + path + "':" + e.what());
			}
		}

		std::string requestedRoot = window_.consumeReRootRequest();
		if (!requestedRoot.empty() && requestedRoot != currentRootKey_) {
			try {
				currentRootKey_ = requestedRoot;
				loadNeighborhood(currentRootKey_, neighborhoodDepth_);
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to load neighborhood: ") + e.what());
			}
		}

		if (auto newSource = window_.consumeNewSourceRequest()) {
			try {
				db_->save(*newSource);
				knownSources_.push_back(*newSource);
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to create source: ") + e.what());
			}
		}

		if (auto newPerson = window_.consumeNewPersonRequest()) {
			try {
				std::string sourceId = newPerson->sourceId;
				db_->save(*newPerson, sourceId);
				currentRootKey_ = sourceId + ":" + newPerson->id;
				// fix id generation
				loadNeighborhood(currentRootKey_, neighborhoodDepth_);
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to create person: ") + e.what());
			}
		}

		if (auto newFact = window_.consumeNewFactRequest()) {
			try {
				const gedcomx::Person& person = newFact->first;
				const std::string& sourceId = newFact->second;
				db_->save(person, sourceId);

				if (!currentRootKey_.empty()) {
					loadNeighborhood(currentRootKey_, neighborhoodDepth_);  // refresh so the panel shows the new fact
				}
			}
			catch (std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to add fact: ") + e.what());
			}
		}

		for (int p = 0; p < 2; ++p) {
			if (auto searchReq = window_.consumePersonSearchQuery(p)) {
				try {
					const std::string& query = searchReq->first;
					const std::string& sourceFilter = searchReq->second;
					auto results = query.empty() ? std::vector<gedcomx::Person>{} : db_->searchPersonsByName(query, sourceFilter);
					window_.setPersonSearchResults(p, results);
				}
				catch (const std::exception& e) {
					Logger::log(LogLevel::Error, std::string("Person search failed: ") + e.what());
				}
			}
		}

		if (auto newRelationship = window_.consumeNewRelationshipRequest()) {
			try {
				const gedcomx::Relationship& rel = newRelationship->first;
				const std::string& sourceId = newRelationship->second;
				db_->save(rel, sourceId);

				if (!currentRootKey_.empty()) {
					loadNeighborhood(currentRootKey_, neighborhoodDepth_);  // refresh to pick up the new edge
				}
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to create relationship: ") + e.what());
			}
		}
	
		if (auto exportSourceId = window_.consumeExportSourceRequest()) {
			pendingExportSourceId_ = *exportSourceId;

			auto it = std::find_if(knownSources_.begin(), knownSources_.end(),
				[&](const gedcomx::SourceDescription& s) {
					return s.id == *exportSourceId;
				});
			std::string suggestedName = (it != knownSources_.end() ? it->primaryTitle() : *exportSourceId) + ".json";

			window_.requestExportSaveDialog(suggestedName);
		}

		if (auto savePath = window_.consumeExportSavePath()) {
			try {
				auto it = std::find_if(knownSources_.begin(), knownSources_.end(),
					[&](const gedcomx::SourceDescription& s) {
						return s.id == pendingExportSourceId_;
					});
				if (it == knownSources_.end()) {
					Logger::log(LogLevel::Error, "Export failed: source no longer known.");
				}
				else {
					auto exportData = db_->gatherSourceExport(pendingExportSourceId_);
					nlohmann::json doc = gedcomx::serializeSourceExport(*it, exportData.persons, exportData.relationships);

					std::ofstream out(*savePath);
					out << doc.dump(2);
				}
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Export failed: ") + e.what());
			}
		}
	}
}