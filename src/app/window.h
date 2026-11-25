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
#include "gedcomx/parent_child.h"
#include "gedcomx/citation.h"
#include "gedcomx/date_parts.h"
#include "gedcomx/place_logic.h"
#include "gedcomx/event.h"

#include "camera.h"
#include "app_settings.h"

struct EditableNote {
	std::string lang;
	std::string subject;  // kept as loaded; the UI only edits the text
	char text[2048] = "";
};

enum class ReferencesKind { Source, Citation };

struct ReferencesRequest {
	ReferencesKind kind;
	std::string id;
};

struct EditableAttribute {
	std::string id;  // kept as loaded; new rows get one when collected
	char type[128] = "";
	char value[256] = "";
};

struct EditablePlaceName {
	char value[256] = "";
	char lang[16] = "";
	gedcomx::Date date;
	char dateText[128] = "";
};

struct EditablePlaceParent {
	std::string parentId;
	gedcomx::Date date;
	char dateText[128] = "";
};

struct EditablePlaceUrl {
	char url[512] = "";
	char description[256] = "";
	char type[64] = "";
};

struct EventChange {
	gedcomx::Event event;  // a loaded event keeps its row keys; a new one has local ids
	std::string sourceId;
	std::string personRowKey;  // who editor asked, so App can refresh that list
};

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
	void renderExportModal(const std::vector<gedcomx::SourceDescription>& knownSources);
	void requestExportModal() { showExportModal_ = true; }
	void requestExportSaveDialog(const std::string& suggestedFileName);

	// Modal window state
	void requestNewSourceEditor() { openSourceEditor(SourceEditorOwner::App, nullptr); }
	void requestEditSourceEditor(const gedcomx::SourceDescription& source) { openSourceEditor(SourceEditorOwner::App, &source); }
	std::optional<gedcomx::SourceDescription> consumeSourceSave();
	// std::optional<std::pair<std::vector<gedcomx::Relationship>, std::string>> consumeNewRelationshipRequest();
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

	// Relationship window
	void requestNewRelationshipEditor();
	void requestEditRelationshipEditor(const gedcomx::Relationship& relationship, const gedcomx::Person& person1, const gedcomx::Person& person2);
	void renderRelationshipEditor(const std::vector<gedcomx::SourceDescription>& knownSources);
	struct ExistingChild {
		gedcomx::Person child;
		std::optional<gedcomx::Relationship> link[2];  // [0] = from Person 1, [1] = from Person 2
	};
	std::optional<std::pair<std::string, std::string>> consumeChildLoadRequest();  // (parent1Key, parent2Key)
	void setExistingChildren(const std::vector<ExistingChild>& children);
	struct RelationshipSaveResult {
		gedcomx::Relationship couple;
		std::vector<gedcomx::Relationship> childLinks;
		std::vector<std::string> deleteLinksIds;
		std::string sourceId;
		bool isEdit = false;
		std::vector<gedcomx::Person> newPersons;  // new children, save before the links
		bool saveCouple = true;  // false for the parent-first editor: onlt children and links are saved
	};
	std::optional<RelationshipSaveResult> consumeRelationshipSaveResult();
	void requestNewChildPersonEditor();  // the "+ New Person" in the Children tab
	void requestAddChildren(const gedcomx::Person& parent);  // parent-first "edit children" for one person

	// Delete
	struct DeleteRequest {
		enum class Kind { Person, Relationship } kind;
		std::string key;  // row key, e.g. "S1:P1" or "S1:R1"
		std::string label;  // show in the confirmation
	};
	void requestDelete(DeleteRequest::Kind kind, const std::string& key, const std::string& label);
	void renderDeleteConfirmModal();
	std::optional<DeleteRequest> consumeDeleteRequest();

	// View mode
	ViewMode viewMode() const { return viewMode_; }
	std::string viewModeSourceId() const { return viewModeSourceId_; }

	// Go to person
	void requestGoToPersonModal() {
		showGoToModal_ = true;
		goToSearchBuffer_[0] = '\0';
		goToSearchResults_.clear();
		selectedGoToIndex_ = -1;
	}
	void renderGoToPersonModal(const std::vector<gedcomx::SourceDescription>& knownSources);
	std::optional<std::string> consumeGoToSearchQuery();
	void setGoToSearchResults(const std::vector<gedcomx::Person>& results);
	std::optional<std::string> consumeGoToPersonRequest();  // row key to jump to

	void setKnownCitations(const std::vector<gedcomx::Citation>& citations) {
		knownCitations_ = citations;
	}
	std::optional<gedcomx::Citation> consumeCitationSave();

	std::optional<ReferencesRequest> consumeReferencesRequest() {
		auto request = pendingReferencesRequest_;
		pendingReferencesRequest_.reset();
		return request;
	}
	void setReferences(ReferencesKind kind, const std::string& id, std::vector<storage::Reference> refs);

	// Places
	void setKnownPlaces(const std::vector<gedcomx::PlaceDescription>& places);
	void requestPlaceList() { showPlaceList_ = true; }
	void requestNewPlaceEditor() { openPlaceEditor(PlaceEditorOwner::App, nullptr); }
	std::optional<gedcomx::PlaceDescription> consumePlaceSave();

	// Events (shared, multi-person records)
	std::optional<std::string> consumePersonEventsRequest() {
		auto request = pendingPersonEventsRequest_;
		pendingPersonEventsRequest_.reset();
		return request;
	}
	void setPersonEvents(const std::string& personRowKey, std::vector<gedcomx::Event> events, std::unordered_map<std::string, std::string> personNames);
	std::optional<EventChange> consumeEventChange();
	std::optional<std::string> consumeSourceEventsRequest() {
		auto request = pendingSourceEventsRequest_;
		pendingSourceEventsRequest_.reset();
		return request;
	}
	void setSourceEvents(const std::string& sourceId, std::vector<gedcomx::Event> events, const std::unordered_map<std::string, std::string>& personNames);

private:
	SDL_Window* window_ = nullptr;
	SDL_Renderer* renderer_ = nullptr;
	bool running_ = true;
	bool showLogWindow_ = false;
	static constexpr float kTitleBarHeight = 32.0f;
	static constexpr float kWindowButtonWidth = 40.0f;
	static constexpr int kResizeBorder = 6;
	static constexpr int kMinWindowWidth = 720;
	static constexpr int kMinWindowHeight = 420;

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

	// Window moving and resizing is done by the OS; this tells it where the title strip and edges are
	static SDL_HitTestResult SDLCALL hitTestCallback(SDL_Window* window, const SDL_Point* area, void* data);
	std::atomic<int> menuRightEdge_{ 300 };  // the title strip is draggable to the right of this (set while drawing the menu)
	int pressedWindowButton_ = -1;
	void renderWindowButtons();
	std::string reRootRequestId_;

	// Layout state
	std::unordered_map<std::string, ImVec2> nodePositions_;
	std::unordered_map<std::string, ImVec2> nodeVelocities_;

	// Layout methods
	void updateLayout(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships);

	// Node drag state
	std::string draggedNodeId_;
	ImVec2 dragStartMousePos_;  // used to distinguish click from drag
	ImVec2 rightClickStartPos_;
	bool rightClickArmed_ = false;  // press started on the canvas3
	std::optional<gedcomx::Relationship> contextRelationship_;  // copy, not a pointer

	// Fact display state
	std::string selectedPersonId_;
	void renderDetailsPanel(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships, const std::vector<gedcomx::SourceDescription>& knownSources, const std::unordered_map<std::string, std::vector<std::string>>& aggregateSourceIds);

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

	// Source Modal state
	std::string pendingAutoSelectSourceId_;
	std::optional<gedcomx::SourceDescription> sourceSaveResult_;

	enum class SourceEditorOwner { None, App, PersonEditor };

	SourceEditorOwner sourceEditorOwner_ = SourceEditorOwner::None;
	bool showSourceEditor_ = false;
	gedcomx::SourceDescription workingSource_;  // empty id = a new source
	char sourceTitleBuffer_[256] = "";
	char sourceAuthorBuffer_[256] = "";
	char sourcePubInfoBuffer_[512] = "";
	char sourceAbbrevBuffer_[64] = "";
	std::vector<EditableNote> sourceNotes_;
	std::vector<EditableAttribute> sourceAttributes_;
	std::vector<EditableAttribute> citationAttributes_;

	void renderEditableAttributes(std::vector<EditableAttribute>& attributes);
	static void fillEditableAttributes(std::vector<EditableAttribute>& out, const std::vector<gedcomx::Attribute>& attributes);
	static std::vector<gedcomx::Attribute> collectAttributes(const std::vector<EditableAttribute>& attributes);

	void openSourceEditor(SourceEditorOwner owner, const gedcomx::SourceDescription* existing);
	void renderSourceEditor(SourceEditorOwner owner);

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
	char factDateBuffer_[128] = "";
	char factPlaceBuffer_[128] = "";
	int editingFactIndex_ = -1;  // -1 = adding; >=0 = editing that index
	gedcomx::Date factDate_;  // the structure date; the buffer is just what the text box show
	std::string factPlaceId_;  // the linked place ("" = none); factPlaceBuffer_ stays what the source says

	void resetFactForm();
	bool renderPlacePicker(const char* id, std::string& placeId, const std::unordered_set<std::string>* excluded, float width);

	int selectedSourceIndex_ = -1;

	// --- Citation Editor (one popup, shared by the Person and Relationship editors) ---
	enum class CitationEditorOwner { None, Person, Relationship, Place, Event };

	std::vector<gedcomx::Citation> knownCitations_;  // pushed by App; includes bare citations
	std::string lastCitedSourceId_;  // preselected the next time the editor opens
	CitationEditorOwner citationEditorOwner_ = CitationEditorOwner::None;
	bool showCitationEditor_ = false;
	bool citationSourceLocked_ = false;  // true when editing an existing citation from the list
	std::string pendingCitationSourceId_;  // resolved against knownSources on the next frame
	int citationSourceIndex_ = -1;
	int citationConfidenceIndex_ = 0;
	gedcomx::Citation workingCitation_;  // empty id = a new citation
	char citationPageBuffer_[256] = "";
	char citationDateBuffer_[128] = "";
	gedcomx::Date citationDate_;  // the structure date; the buffer above is just what the text box shows

	std::vector<EditableNote> citationNotes_;
	std::optional<gedcomx::Citation> citationSaveResults_;

	void renderCitationRefs(CitationEditorOwner owner,
		std::vector<gedcomx::SourceReference>& refs,
		const std::vector<gedcomx::SourceDescription>& knownSources);
	void openCitationEditor(CitationEditorOwner owner, const gedcomx::Citation* existing);
	void renderCitationEditor(CitationEditorOwner owner, const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderEditableNotes(std::vector<EditableNote>& notes);
	static void fillEditableNotes(std::vector<EditableNote>& out, const std::vector<gedcomx::Note>& notes);
	static std::vector<gedcomx::Note> collectNotes(const std::vector<EditableNote>& notes);
	void fillCitationForm(const gedcomx::Citation& c);
	void startNewCitationForm();
	std::vector<gedcomx::SourceReference>& citationTargetRefs();

	std::optional<std::pair<gedcomx::Person, std::string>> personEditResult_;

	void renderPersonEditorTopStrip(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderPersonEditorNamesTab();
	void renderPersonEditorCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderPersonEditorAttributesTab();
	void renderPersonEditorAddressesTab();

	// Relationship Modal state
	gedcomx::Relationship workingRelationship_;
	bool showNewRelationshipModal_ = false;
	std::string relationshipSourceId_;  // set once Person 1 is picked; scopes Person 2's search 
	int relEditingFactIndex_ = -1;
	int relSelectedFactTypeIndex_ = 0;
	char relFactTypeCustomBuffer_[256] = "";
	char relFactDateBuffer_[128] = "";
	char relFactPlaceBuffer_[128] = "";
	gedcomx::Date relFactDate_;  // the structured date; the buffer is just what the text box shows
	std::string relFactPlaceId_;  // the linked place ("" = none)
	void resetRelFactForm();

	// Relationship Type Modal state
	std::optional<std::pair<std::vector<gedcomx::Relationship>, std::string>> newRelationshipResult_;
	bool editingExistingRelationship_ = false;
	std::optional<std::pair<gedcomx::Relationship, std::string>> editRelationshipResult_;

	void renderRelationshipEditorTopStrip(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderRelationshipEditorChildrenTab();
	void renderRelationshipEditorEventsTab();
	void renderRelationshipEditorCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderRelationshipEditorAttributesTab();

	// Relationship Children tab
	// widen the search arrays by one slot: [2] = the Children tab's child search
	char personSearchBuffer_[3][128] = { "", "", "" };
	std::vector<gedcomx::Person> personSearchResults_[3];
	int selectedPersonIndex_[3] = { -1, -1, -1 };
	std::optional<std::string> pendingSearchQuery_[3];

	struct StagedChild {
		gedcomx::Person person;
		std::optional<gedcomx::Relationship> link[2];  // existing ParentChild link per parent, if any
		int parentage[2] = { 1, 1 };  // index into kParentage; 1 = Unspecified
		bool isNew = false;  // not in the database yet; save with the couple 
	};
	std::vector<StagedChild> stagedChildren_;
	std::vector<gedcomx::Relationship> removedChildLinks_;  // existing links to delete on Save
	std::optional<std::pair<std::string, std::string>> pendingChildLoad_;
	void resetChildrenTabState();
	std::optional<RelationshipSaveResult> relationshipSaveResult_;

	bool personEditorForChild_ = false;  // Person Editor is acting as "add child" for the Relationship Editor
	bool showChildrenEditor_ = false;
	gedcomx::Person soloParent_;
	void renderChildrenEditor(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderChildrenEditorBody(const gedcomx::Person* parent1, const gedcomx::Person* person2);  // parent2 may be null
	std::vector<gedcomx::ChildLinkInput> collectChildInputs(int slotCount, std::vector<gedcomx::Person>& newPersons) const;

	// Delete
	bool showDeleteConfirmModal_ = false;
	std::optional<DeleteRequest> pendingDelete_;  // what the modal is asking about
	std::optional<DeleteRequest> deleteResult_;  // set only when the user confirms

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

	// Person Search Modal
	bool showGoToModal_ = false;
	char goToSearchBuffer_[128] = "";
	std::vector<gedcomx::Person> goToSearchResults_;
	int selectedGoToIndex_ = -1;
	std::optional<std::string> pendingGoToSearchQuery_;
	std::optional<std::string> goToPersonResult_;

	std::optional<ReferencesRequest> pendingReferencesRequest_;
	std::vector<storage::Reference> sourceReferences_;
	std::string sourceReferencesFor_;  // the id the list belongs to, so a stale answer is never shown
	std::vector<storage::Reference> citationReferences_;
	std::string citationReferencesFor_;
	void renderReferencesTab(const std::vector<storage::Reference>& refs, bool showCitationColumn);

	// --- Date Editor (one popup, opened from any date field) ---
	enum class DateEditorTarget { None, Citation, PlaceName, PlaceParent, PersonFact, RelationshipFact, Event };

	DateEditorTarget dateEditorTarget_ = DateEditorTarget::None;
	bool showDateEditor_ = false;
	int dateKindIndex_ = 0;  // same order as gedcomx::DateKind
	int dateCalendarIndex_ = 0;
	int dateQualityIndex_ = 0;
	gedcomx::SimpleDate dateStart_;
	gedcomx::SimpleDate dateStop_;
	char dateTextBuffer_[256] = "";

	void openDateEditor(DateEditorTarget target, const gedcomx::Date& initial);
	std::optional<gedcomx::Date> renderDateEditor(DateEditorTarget target);  // the new date on the frame OK is pressed
	gedcomx::DateParts dateEditorParts() const;

	// --- Place List and Place Editor ---
	enum class PlaceEditorOwner { None, App, List };

	std::vector<gedcomx::PlaceDescription> knownPlaces_;  // pushed by App
	gedcomx::PlaceIndex placeIndex_;  // points into knownPlaces_; rebuilt whenever that changes
	std::vector<std::pair<std::string, std::string>> placePickList_;  // (title, id), sorted by title

	bool showPlaceList_ = false;
	char placeFilterBuffer_[128] = "";
	std::string selectedPlaceId_;

	PlaceEditorOwner placeEditorOwner_ = PlaceEditorOwner::None;
	bool showPlaceEditor_ = false;
	gedcomx::PlaceDescription workingPlace_;  // empty id = a new place; keeps what the editor does not show
	std::optional<gedcomx::PlaceDescription> placeSaveResult_;
	std::vector<EditablePlaceName> placeNames_;
	std::vector<EditablePlaceParent> placeParents_;
	std::vector<EditablePlaceUrl> placeUrls_;
	std::vector<EditableNote> placeNotes_;
	char placeTypeBuffer_[64] = "";
	char placeCodeBuffer_[64] = "";
	char placeLatBuffer_[32] = "";
	char placeLonBuffer_[32] = "";
	char placeParentFilterBuffer_[64] = "";
	std::unordered_set<std::string> placeParentExcluded_;  // the place itself and everything below it
	int placeDateRow_ = -1;  // the row whose "..." opened the Date editor

	void openPlaceEditor(PlaceEditorOwner owner, const gedcomx::PlaceDescription* existing);
	void renderPlaceEditor(PlaceEditorOwner owner, const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderPlaceList(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderPlaceNamesTab();
	void renderPlaceLocationTab();
	void renderPlaceParentsTab();
	void renderPlaceLinksTab();
	void renderPlaceCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources);
	std::string collectPlace(gedcomx::PlaceDescription& out) const;  // first problem, or "" when out can be saved
	std::string placeTitleById(const std::string& id) const;
	static bool renderDateInput(const char* id, gedcomx::Date& date, char* buffer, size_t bufferSize, float width);

	// --- Events tab of the Person editor ---
	std::optional<std::string> pendingPersonEventsRequest_;
	std::string personEventsFor_;  // the person (row key) the list below belongs to
	std::vector<gedcomx::Event> personEvents_;
	std::unordered_map<std::string, std::string> eventPersonNames_;  // person row key -> name
	std::optional<EventChange> eventChangeResult_;

	std::string personEditorRowKey() const;  // "sourceId:id" of the person being edited, "" for a new one
	void renderPersonEditorFactsTab();
	void renderPersonEditorEventsTab();  // shared-events tab

	// --- Event Editor: the person's reference to a shared event ---
	bool showEventEditor_ = false;
	gedcomx::Event workingEvent_;  // loaded (row keeys) or new (empty id); keeps other people's roles
	std::string eventEditorPersonKey_;
	int eventMyRoleIndex_ = -1;  // index into workingEvent_.roles; -1 = this person is being added
	int eventTypeIndex_ = 0;
	char eventTypeCustomBuffer_[256] = "";
	char eventDescriptionBuffer_[256] = "";
	gedcomx::Date eventDate_;
	char eventDateBuffer_[128] = "";
	char eventPlaceBuffer_[128] = "";
	std::string eventPlaceId_;
	int eventRoleIndex_ = 0;
	char eventRoleCustomBuffer_[256] = "";
	char eventRoleDetailsBuffer_[256] = "";
	std::vector<EditableNote> eventNotes_;

	std::optional<std::string> pendingSourceEventsRequest_;
	std::string sourceEventsFor_;
	std::vector<gedcomx::Event> sourceEvents_;
	bool showExistingEventPicker_ = false;
	std::string existingEventSelected_;
	char existingEventFilter_[64] = "";

	void openEventEditor(const gedcomx::Event* existing);
	void renderEventEditor(const std::vector<gedcomx::SourceDescription>& knownSources);
	void renderExistinEventPicker();
	std::string collectEvent(gedcomx::Event& out) const;  // first problem, or "" when out can be saved
};