#pragma	once

#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>
#include <optional>
#include <functional>

#include <sdl3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "gedcomx/person.h"
#include "gedcomx/relationship.h"
#include "gedcomx/fact.h"
#include "gedcomx/source_description.h"
#include "storage/database.h"

#include "camera.h"
#include "app_settings.h"

class Window {
public:
	Window();
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	enum class ViewMode { AllSources, Aggregate, SingleSource };

	bool isRunning() const;
	void processEvents();
	void beginFrame();
	void renderTitleBarAndMenu(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderCanvas(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships, const std::vector<gedcomx::SourceDescription>& knownSources, const std::unordered_map<std::string, std::vector<std::string>>& aggregateSourceIds);
	void endFrame();

	// Window resize and reframe
	std::string consumeReRootRequest();
	void requestRefit() { needsRefit_ = true; }
	
	// File menu requests
	void requestOpenFileDialog();
	std::vector<std::string> consumeOpenedFiles();
	void requestOpenProjectDialog();
	std::optional<std::string> consumeOpenProjectRequest();
	void setProjectOpen(bool open) { projectOpen_ = open; }
	void requestNewProjectDialog();
	std::optional<std::pair<std::string, std::string>> consumeNewProjectRequest();
	void requestSaveProject() { saveProjectRequested_ = true; }
	bool consumeSaveProjectRequest();

	// Call this instead of acting immediately, whenever a New/Open Project
	// request meeds to first confirm discarding unsaved session state.
	void confirmProjectSwitch(std::function<void()> onConfirmed);

	// Camera state
	void setCameraState(float x, float y, float zoom) { camera_.setState(ImVec2(x, y), zoom); }
	ImVec2 cameraOffset() const { return camera_.offset(); }
	float cameraZoom() const { return camera_.zoom(); }

	// Error Logging
	void renderLogWindow();
	void toggleLogWindow() { showLogWindow_ = !showLogWindow_; }

	// Physics Settings
	void setPhysicsSettings(const AppSettings& settings) { settings_ = settings; }
	AppSettings physicsSettings() const { return settings_; }

	// Settings Panel  
	void renderSettingsPanel(int currentNeighborhoodDepth, size_t currentMaxVisibleNodes);
	void toggleSettingsPanel() { showSettingsPanel_ = !showSettingsPanel_; }
	bool consumeSaveSettingsRequest();

	// Per-project settings: Window displayss current values (passed in each
	// frame) and reports back if the user changed something - App owns the
	// actual values and decides what to do with a change (e.g. re-root)
	std::optional<int> consumeNeighborhoodDepthChange();
	std::optional<size_t> consumeMaxVisibleNodesChange();

	// Modal window methods
	void renderCreationModals(const std::vector<gedcomx::SourceDescription>& knownSources);
	void requestNewRelationshipModal() { showNewRelationshipModal_ = true; }
	void renderExportModal(const std::vector<gedcomx::SourceDescription>& knownSources);
	void requestExportModal() { showExportModal_ = true; }
	void requestExportSaveDialog(const std::string& suggestedFileName);

	// Modal window state
	std::optional<gedcomx::SourceDescription> consumeNewSourceRequest();
	std::optional<std::pair<gedcomx::Relationship, std::string>> consumeNewRelationshipRequest();
	std::optional<std::string> consumeExportSourceRequest();
	std::optional<std::string> consumeExportSavePath();

	// Window need to ask App to search storage - same request/consume shape
	// but this one needs a return value each frame, not just a fire-and-forget.
	std::optional<std::pair<std::string, std::string>> consumePersonSearchQuery(int pickerIndex);
	void setPersonSearchResults(int pickerIndex, const std::vector<gedcomx::Person>& results);

	// Person window
	void requestNewPersonEditor();
	void requestEditPersonEditor(const gedcomx::Person& person);
	void renderPersonEditor(const std::vector<gedcomx::SourceDescription>& knownSources);
	std::optional<std::pair<gedcomx::Person, std::string>> consumePersonEditResult();

	// Aggregate layer toggle
	void requestLinkPersonModal(const std::string& personRowKey) {
		showLinkPersonModal_ = true;
		linkSourcePersonKey_ = personRowKey;
	}
	std::optional<std::pair<std::string, std::string>> consumeLinkPersonRequest();  // (personA, personB)
	void renderLinkPersonModal();
	std::optional<std::string> consumeLinkSearchQuery();
	void setLinkSearchResults(const std::vector<gedcomx::Person>& results);

	// View mode
	ViewMode viewMode() const { return viewMode_; }
	std::string viewModeSourceId() const { return viewModeSourceId_; }

private:
	SDL_Window* window_ = nullptr;
	SDL_Renderer* renderer_ = nullptr;
	bool running_ = true;
	bool showLogWindow_ = false;
	static constexpr float kTitleBarHeight = 32.0f;

	struct EditableNameEntry {
		char given[128] = "";
		char surname[128] = "";
	};

	// Draws a list of Selectable rows, giving each a unique ImGui ID via
	// "displayText##uniqueId" so items with identical display text (e.g.
	// two people both named "Adam") don't collide - see ImGui's ID-stack
	// rules. getDisplay/getId are called once per item, per frame.
	template <typename T>
	void renderSelectableList(const std::vector<T>& items, int* selectedIndex, const std::function<std::string(const T&)>& getDisplay, const std::function<std::string(const T&)> getId) {
		for (int i = 0; i < (int)items.size(); ++i) {
			bool isSelected = (i == *selectedIndex);
			std::string label = getDisplay(items[i]) + "##" + getId(items[i]);
			if (ImGui::Selectable(label.c_str(), isSelected)) {
				*selectedIndex = i;
			}
		}
	}

	// Window movement and position
	bool draggingWindow_ = false;
	float dragStartMouseX_ = 0, dragStartMouseY_ = 0;
	int dragStartWindowX_ = 0, dragStartWindowY_ = 0;
	std::string reRootRequestId_;

	// Layout state
	std::unordered_map<std::string, ImVec2> nodePositions_;
	std::unordered_map<std::string, ImVec2> nodeVelocities_;

	// Layout methods
	void updateLayout(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships);

	// Node drag state
	std::string draggedNodeId_;
	ImVec2 dragStartMousePos_;  // used to distinguish click from drag

	// Fact display state
	std::string selectedPersonId_;
	void renderDetailsPanel(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::SourceDescription>& knownSources, const std::unordered_map<std::string, std::vector<std::string>>& aggregateSourceIds);

	// Camera stuff
	Camera camera_;
	bool needsRefit_ = true;  // set true whenever the visible node set changes

	// File menu requests
	std::mutex openedFilesMutex_;
	std::vector<std::string> openedFiles_;  // pending files, filled by the async callback
	static void handleFileDialogResult(void* userdata, const char* const* filelist, int filter);
	
	// Open Projet (.whproj file dialog)
	std::mutex openProjectMutex_;
	std::optional<std::string> pendingOpenProjectPath_;
	static void handleOpenProjectDialogResult(void* usedata, const char* const* filelist, int filter);
	bool projectOpen_ = false;

	// New Projet (folder dialog, then a name-prompt modal)
	std::mutex newProjectFolderMutex_;
	std::optional<std::string> pendingNewProjectFolder_;
	static void handleNewProjectFolderDialogResult(void* userdata, const char* const* filelist, int filter);
	bool showNewProjectNameModal_ = false;
	std::string newProjectFolder_;
	char newProjectNameBuffer_[128] = "";
	std::optional<std::pair<std::string, std::string>> newProjectResult_;  // (folder, name) 

	bool saveProjectRequested_ = false;
	bool switchConfirmRequested_ = false;

	void renderNewProjectModal();

	// Switch projects
	bool showSwitchConfirmModal_ = false;
	std::function<void()> pendingSwitchAction_;  // what to do if the user confirms 
	void renderSwitchConfirmModal();

	// App settings
	AppSettings settings_;
	bool showSettingsPanel_ = false;
	bool saveSettingsRequested_ = false;

	// Per-project settings
	std::optional<int> neighborhoodDepthChange_;
	std::optional<size_t> maxVisibleNodesChange_;

	// View Mode
	ViewMode viewMode_ = ViewMode::AllSources;
	std::string viewModeSourceId_;

	// Modal window stuff 
	void renderNewSourceModal();
	void renderNewRelationshipModal();

	// Source Modal state
	std::string pendingAutoSelectSourceId_;
	char newSourceTitleBuffer_[256] = "";
	std::optional<gedcomx::SourceDescription> newSourceResult_;

	// Person Modal state
	bool showPersonEditor_ = false;
	bool editingExistingPerson_ = false;
	gedcomx::Person workingPerson_;
	std::vector<EditableNameEntry> editNames_;
	
	char primaryGivenBuffer_[128] = "";
	char primarySurnameBuffer_[128] = "";
	char primaryTitleBuffer_[128] = "";
	char primaryNicknameBuffer_[128] = "";
	char primaryCallNameBuffer_[128] = "";
	int genderIndex_ = 2;  // 0=Female, 1=Male, 2=Unknown, 3=Other

	int editingAttributeIndex_ = -1;
	char attrTypeBuffer_[128] = "";
	char attrValueBuffer_[256] = "";

	int editingAddressIndex_ = -1;
	char addrStreetBuffer_[128] = "";
	char addrCityBuffer_[1128] = "";
	char addrStateBuffer_[128] = "";
	char addrPostalBuffer_[128] = "";
	char addrCountryBuffer_[64] = "";

	char factTypeCustomBuffer_[128] = "";
	int selectedFactTypeIndex_ = 0;
	char factDateBuffer_[64] = "";
	char factPlaceBuffer_[128] = "";
	int editingFactIndex_ = -1;  // -1 = adding; >=0 = editing that index

	int selectedSourceIndex_ = -1;
	int selectedSourceToAddIndex_ = -1;  // Sources tab's "add citation picker"

	std::optional<std::pair<gedcomx::Person, std::string>> personEditResult_;

	void renderPersonEditorTopStrip(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderPersonEditorEventsTab();
	void renderPersonEditorNamesTab();
	void renderPersonEditorCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderPersonEditorAttributesTab();
	void renderPersonEditorAddressessTab();

	// Relationship Modal state
	bool showNewRelationshipModal_ = false;
	char personSearchBuffer_[2][128] = { "", "" };  // [0] = person1, [1] = person2
	std::vector<gedcomx::Person> personSearchResults_[2];
	int selectedPersonIndex_[2] = { -1, -1 };
	std::optional<std::string> pendingSearchQuery_[2];
	std::string relationshipSourceId_;  // set once Person 1 is picked; scopes Person 2's search 

	// Relationship Type Modal state
	char relationshiptypeCustomBuffer_[128] = "";
	int selectedRelationshipTypeIndex_ = 0;
	std::optional<std::pair<gedcomx::Relationship, std::string>> newRelationshipResult_;

	// Export Modal
	bool showExportModal_ = false;
	int selectedExportSourceIndex_ = -1;
	std::optional<std::string> exportSourceResult_;
	std::mutex exportSaveMutex_;
	std::optional<std::string> pendingExportSavePath_;
	std::string pendingExportSourceId_;  // which source the sae dialog is for
	static void handleExportSaveDialogResult(void* userdata, const char* const* filelist, int filter);

	// Aggregate Layer state
	bool showLinkPersonModal_ = false;
	std::string linkSourcePersonKey_;
	char linkSearchBuffer_[128] = "";
	std::vector<gedcomx::Person> linkSearchResults_;
	int selectedLinkIndex_ = -1;
	std::optional<std::string> pendingLinkSearchQuery_;
	std::optional<std::pair<std::string, std::string>> linkPersonResult_;
};