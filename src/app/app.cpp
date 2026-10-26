#include <filesystem>
#include <fstream>
#include <queue>
#include <unordered_set>
#include <limits>
#include <algorithm>
#include <iostream>
#include <tuple>

#include "app.h"
#include "logger.h"
#include "app_settings.h"
#include "id_generator.h"
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

	// Third element: was this entry reached via a cross-source link? If so,
	// it still gets to explore its own relationships once, even past the
	// normal depth cut-off - otherwise linking two records only ever reveals
	// the linked person, never anything connected to them on that side.
	std::queue<std::tuple<std::string, int, bool>> toVisit;  // (personKey, currentDepth, viaLink)
	toVisit.push({ rootPersonKey, 0, false });
	visitedPersons.insert(rootPersonKey);

	while (!toVisit.empty()) {
		auto [personKey, currentDepth, viaLink] = toVisit.front();
		toVisit.pop();

		auto person = db_->loadPerson(personKey);
		if (person) visiblePeopleMap_[personKey] = *person;  // union: insert-or-update, nothing removed here

		// Cross-source identity links are "free" - following one doesn't
		// consume depth budget, since it's not a genealogical distance,
		// it's the same person described by a difference source.
		try {
			for (const auto& linkedKey : db_->getLinkedPersonKeys(personKey)) {
				if (visitedPersons.insert(linkedKey).second) {
					toVisit.push({ linkedKey, currentDepth, true });
				}
			}
		}
		catch (const std::exception& e) {
			Logger::log(LogLevel::Error, std::string("Failed to resolve linked persons: ") + e.what());
		}

		if (currentDepth >= depth && !viaLink) continue;  //stop expanding past the requested depth

		for (const auto& rel : db_->loadRelationshipsForPerson(personKey)) {
			const std::string relKey = rel.sourceId + ":" + rel.id;
			if (visitedRelationships.insert(relKey).second) {
				visibleRelationshipsMap_[relKey] = rel;
			}

			// Whichever side isn't the person we came from is the neighbor to explore next.
			const std::string& neighborKey = (rel.person1Id == personKey) ? rel.person2Id : rel.person1Id;
			if (visitedPersons.insert(neighborKey).second) {
				toVisit.push({ neighborKey, currentDepth + 1, false });
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

	aggregateViewDirty_ = true;  // whatever the toggle shows next needs recomputing
}

void App::rebuildAggregateView() {
	aggregateVisiblePeople_.clear();
	aggregateContributingSourceIds_.clear();
	aggregateVisibleRelationships_.clear();

	if (!db_) {
		aggregateViewDirty_ = false;
		return;
	}

	// Resolve each currently visible person's "group identity": their
	// aggregate_id if manually linked, or their own row-key if not linked
	// (an unlinked person is treated as a singleton group of one).
	std::unordered_map<std::string, std::string> personToIdentity;
	std::unordered_map<std::string, std::string> identityToRepresentative;
	std::unordered_map<std::string, std::vector<gedcomx::Person>> identityGroups;

	for (const auto& person : visiblePeople_) {
		std::string identity;

		try {
			auto aggId = db_->getAggregateIdForPerson(person.id);
			identity = aggId ? *aggId : person.id;
		}
		catch (const std::exception& e) {
			Logger::log(LogLevel::Error, std::string("Failed to resolve aggregate identity: ") + e.what());
			identity = person.id;
		}

		personToIdentity[person.id] = identity;
		identityGroups[identity].push_back(person);

		// First memeber seen for a group becomes its representative - its
		// real row-key becomes the merged node's id, so re-rooting/"Explore
		// from here" keeps working normally on a merged mode.
		if (identityToRepresentative.find(identity) == identityToRepresentative.end()) {
			identityToRepresentative[identity] = person.id;
		}
	}

	for (const auto& [identity, members] : identityGroups) {
		gedcomx::Person merged;
		merged.id = identityToRepresentative[identity];
		merged.sourceId = members[0].sourceId;  // display-only; a merged node may span sources

		std::vector<std::string> sourceIds;
		for (const auto& m : members) {
			merged.names.insert(merged.names.end(), m.names.begin(), m.names.end());
			merged.facts.insert(merged.facts.end(), m.facts.begin(), m.facts.end());
			if (std::find(sourceIds.begin(), sourceIds.end(), m.sourceId) == sourceIds.end())
				sourceIds.push_back(m.sourceId);
		}
		aggregateContributingSourceIds_[merged.id] = sourceIds;

		aggregateVisiblePeople_.push_back(std::move(merged));
	}

	// Resolve edges: a relationship between two persons who now share a
	// merged identity is internal to that merge and gets droppped; edges
	// between different identities are kept, remapped onto each identity's
	// representative id, and de-duplicated (two source relationships could
	// both connect the same pair of now-merged people).
	std::set<std::pair<std::string, std::string>> seenPairs;
	for (const auto& rel : visibleRelationships_) {
		auto it1 = personToIdentity.find(rel.person1Id);
		auto it2 = personToIdentity.find(rel.person2Id);
		if (it1 == personToIdentity.end() || it2 == personToIdentity.end()) continue;
		if (it1->second == it2->second) continue;  // same merged group - skip

		std::string rep1 = identityToRepresentative[it1->second];
		std::string rep2 = identityToRepresentative[it2->second];

		auto pairKey = rep1 < rep2 ? std::make_pair(rep1, rep2) : std::make_pair(rep2, rep1);
		if (!seenPairs.insert(pairKey).second) continue;

		gedcomx::Relationship aggRel;
		aggRel.id = rel.id;
		aggRel.type = rel.type;
		aggRel.person1Id = rep1;
		aggRel.person2Id = rep2;
		aggregateVisibleRelationships_.push_back(std::move(aggRel));
	}

	aggregateViewDirty_ = false;
}

void App::run() {
	while (window_.isRunning()) {
		window_.processEvents();
		window_.beginFrame();
		window_.renderTitleBarAndMenu(knownSources_);
		switch (window_.viewMode()) {
			case Window::ViewMode::AllSources: {
				window_.renderCanvas(visiblePeople_, visibleRelationships_, knownSources_, {});
				break;
			}
			case Window::ViewMode::SingleSource: {
				std::string filterId = window_.viewModeSourceId();
				std::vector<gedcomx::Person> filteredPeople;
				std::vector<gedcomx::Relationship> filteredRels;
				for (const auto& p : visiblePeople_) if (p.sourceId == filterId) filteredPeople.push_back(p);
				for (const auto& r : visibleRelationships_) if (r.sourceId == filterId) filteredRels.push_back(r);
				window_.renderCanvas(filteredPeople, filteredRels, knownSources_, {});
				break;
			}
			case Window::ViewMode::Aggregate: {
				if (aggregateViewDirty_) rebuildAggregateView();
				window_.renderCanvas(aggregateVisiblePeople_, aggregateVisibleRelationships_, knownSources_, aggregateContributingSourceIds_);
				break;
			}
		}
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
					saveProject();
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
				saveProject();
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

		if (auto editedPerson = window_.consumePersonEditResult()) {
			try {
				gedcomx::Person person = editedPerson->first;
				const std::string& sourceId = editedPerson->second;
				std::string rowKey = sourceId + ":" + person.id;
				bool wasAlreadyVisible = visiblePeopleMap_.find(rowKey) != visiblePeopleMap_.end();

				db_->save(person, sourceId);

				if (wasAlreadyVisible) {
					// loadNeighborhood only refreshes people it walks to, so update this one explicitly
					if (auto fresh = db_->loadPerson(rowKey)) visiblePeopleMap_[rowKey] = *fresh;
				}
				else {
					currentRootKey_ = rowKey;
					saveProject();
				}

				if (!currentRootKey_.empty()) {
					loadNeighborhood(currentRootKey_, neighborhoodDepth_);
				}
				else rebuildVisibleVectors();
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Faield to save person: ") + e.what());
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

		if (auto batch = window_.consumeNewRelationshipRequest()) {
			try {
				const std::vector<gedcomx::Relationship>& relationship = batch->first;
				const std::string& sourceId = batch->second;
				db_->saveAll(relationship, sourceId);

				if (!currentRootKey_.empty()) {
					loadNeighborhood(currentRootKey_, neighborhoodDepth_);  // refresh to pick up the new edge
				}
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to create relationship: ") + e.what());
			}
		}

		if (auto edited = window_.consumeRelationshipEditResult()) {
			try {
				db_->save(edited->first, edited->second);
				if (!currentRootKey_.empty()) loadNeighborhood(currentRootKey_, neighborhoodDepth_);
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to save relationship: ") + e.what());
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

		if (auto linkQuery = window_.consumeLinkSearchQuery()) {
			try {
				auto results = linkQuery->empty() ? std::vector<gedcomx::Person>{} : db_->searchPersonsByName(*linkQuery);
				window_.setLinkSearchResults(results);
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Person search failed: ") + e.what());
			}
		}

		if (auto linkReq = window_.consumeLinkPersonRequest()) {
			try {
				const std::string& personA = linkReq->first;
				const std::string& personB = linkReq->second;

				// Decide the aggregate id: reuse one side's existing group if it
				// has one, otherwise the other side's, otherwise mint a new one.
				auto existingA = db_->getAggregateIdForPerson(personA);
				auto existingB = db_->getAggregateIdForPerson(personB);

				std::string aggregateId;
				if (existingA) aggregateId = *existingA;
				else if (existingB) aggregateId = *existingB;
				else aggregateId = idgen::generateLocalId("AGG_");

				// Parse each row-key back into (sourceId, localId) for linkPersons'
				// expected shape.
				auto splitRowKey = [](const std::string& rowKey) {
					size_t colon = rowKey.find(':');
					return std::make_pair(rowKey.substr(0, colon), rowKey.substr(colon + 1));
				};
				auto [sourceA, localA] = splitRowKey(personA);
				auto [sourceB, localB] = splitRowKey(personB);

				db_->linkPersons(aggregateId, { {sourceA, localA}, {sourceB, localB} });
				aggregateViewDirty_ = true;
			}
			catch (const std::exception& e) {
				Logger::log(LogLevel::Error, std::string("Failed to link persons: ") + e.what());
			}
		}
	}

	try {
		saveProject();
	}
	catch (const std::exception& e) {
		Logger::log(LogLevel::Error, std::string("Failed to save project on exit: ") + e.what());
	}
}