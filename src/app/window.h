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

#include "camera.h"
#include "app_settings.h"

class Window {
public:
	Window();
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	bool isRunning() const;
	void processEvents();
	void beginFrame();
	void renderTitleBarAndMenu();
	void renderCanvas(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships);
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

private:
	SDL_Window* window_ = nullptr;
	SDL_Renderer* renderer_ = nullptr;
	bool running_ = true;
	bool showLogWindow_ = false;
	static constexpr float kTitleBarHeight = 32.0f;

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

	// Fact displaay state
	std::string selectedPersonId_;
	void renderDetailsPanel(const std::vector<gedcomx::Person>& people);

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
};