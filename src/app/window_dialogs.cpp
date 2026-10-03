#include <SDL3/SDL_dialog.h>

#include "window.h"
#include "logger.h"
#include "id_generator.h"
#include "storage/database.h"

namespace {
    const char* kRelationshipTypes[] = {
        "http://gedcomx.org/Couple",
        "http://gedcomx.org/ParentChild",
        "Custom..."
    };
}

// --- Open Source File ---

void Window::requestOpenFileDialog() {
    SDL_DialogFileFilter filters[] = {
        { "GEDCOM X (JSON)", "json" },
        { "All files", "*" }
    };

    SDL_ShowOpenFileDialog(&Window::handleFileDialogResult, this, window_, filters, 2, nullptr, true);
}

void Window::handleFileDialogResult(void* userdata, const char* const* filelist, int filter) {
    Window* self = static_cast<Window*>(userdata);
    if (!filelist) return;

    std::lock_guard<std::mutex> lock(self->openedFilesMutex_);
    for (int i = 0; filelist[i] != nullptr; ++i) {
        self->openedFiles_.push_back(filelist[i]);
    }
}

std::vector<std::string> Window::consumeOpenedFiles() {
    std::lock_guard<std::mutex> lock(openedFilesMutex_);
    std::vector<std::string> result = std::move(openedFiles_);
    openedFiles_.clear();
    return result;
}

// --- Open Project ---

void Window::requestOpenProjectDialog() {
    SDL_DialogFileFilter filters[] = { { "WHP Project", "whproj" } };
    SDL_ShowOpenFileDialog(&Window::handleOpenProjectDialogResult, this, window_, filters, 1, nullptr, false);
}

void Window::handleOpenProjectDialogResult(void* userdata, const char* const* filelist, int filter) {
    Window* self = static_cast<Window*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(self->openProjectMutex_);
    self->pendingOpenProjectPath_ = filelist[0];
}

std::optional<std::string> Window::consumeOpenProjectRequest() {
    std::lock_guard<std::mutex> lock(openProjectMutex_);
    std::optional<std::string> result = pendingOpenProjectPath_;
    pendingOpenProjectPath_.reset();
    return result;
}

// --- New Project ---

void Window::requestNewProjectDialog() {
    SDL_ShowOpenFolderDialog(&Window::handleNewProjectFolderDialogResult, this, window_, nullptr, false);
}

void Window::handleNewProjectFolderDialogResult(void* userdata, const char* const* filelist, int filter) {
    Window* self = static_cast<Window*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(self->newProjectFolderMutex_);
    self->pendingNewProjectFolder_ = filelist[0];
}

void Window::renderNewProjectModal() {
    {
        std::lock_guard<std::mutex> lock(newProjectFolderMutex_);
        if (pendingNewProjectFolder_) {
            newProjectFolder_ = *pendingNewProjectFolder_;
            pendingNewProjectFolder_.reset();
            newProjectNameBuffer_[0] = '\0';
            showNewProjectNameModal_ = true;
            ImGui::OpenPopup("New Project");
        }
    }

    if (ImGui::BeginPopupModal("New Project", &showNewProjectNameModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Project location: %s", newProjectFolder_.c_str());
        ImGui::InputText("Project Name", newProjectNameBuffer_, sizeof(newProjectNameBuffer_));

        bool nameEmpty = newProjectNameBuffer_[0] == '\0';
        ImGui::BeginDisabled(nameEmpty);
        if (ImGui::Button("Create")) {
            newProjectResult_ = { newProjectFolder_, std::string(newProjectNameBuffer_) };
            ImGui::CloseCurrentPopup();
            showNewProjectNameModal_ = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            showNewProjectNameModal_ = false;
        }

        ImGui::EndPopup();
    }
}

std::optional<std::pair<std::string, std::string>> Window::consumeNewProjectRequest() {
    auto result = newProjectResult_;
    newProjectResult_.reset();
    return result;
}

// --- Save Project ---

bool Window::consumeSaveProjectRequest() {
    bool result = saveProjectRequested_;
    saveProjectRequested_ = false;
    return result;
}

// --- Project Switch ---

void Window::confirmProjectSwitch(std::function<void()> onConfirmed) {
    pendingSwitchAction_ = std::move(onConfirmed);
    switchConfirmRequested_ = true;
}

void Window::renderSwitchConfirmModal() {
    if (switchConfirmRequested_) {
        switchConfirmRequested_ = false;
        showSwitchConfirmModal_ = true;
        ImGui::OpenPopup("Confirm Switch Project");
    }

    if (ImGui::BeginPopupModal("Confirm Switch Project", &showSwitchConfirmModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The current project will be saved before switching.");
        ImGui::Spacing();

        if (ImGui::Button("Switch")) {
            if (pendingSwitchAction_) pendingSwitchAction_();
            ImGui::CloseCurrentPopup();
            showSwitchConfirmModal_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pendingSwitchAction_ = nullptr;
            ImGui::CloseCurrentPopup();
            showSwitchConfirmModal_ = false;
        }

        ImGui::EndPopup();
    }
}

// --- Log Window ---

void Window::renderLogWindow() {
    if (!showLogWindow_) return;

    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Log", &showLogWindow_)) {
        ImGui::PushTextWrapPos(0.0f);

        for (const auto& entry : Logger::recentEntries()) {
            ImVec4 color = entry.level == LogLevel::Error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                : entry.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f)
                : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            ImGui::TextColored(color, "%s", entry.message.c_str());
        }

        ImGui::PopTextWrapPos();

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::End();
}

// --- Settings Panel ---

void Window::renderSettingsPanel(int currentNeighborhoodDepth, size_t currentMaxVisibleNodes) {
    if (!showSettingsPanel_) return;

    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &showSettingsPanel_)) {
        ImGui::Text("Physics");
        ImGui::SliderFloat("Replusion", &settings_.repulsionStrength, 1000.0f, 20000.0f);
        ImGui::SliderFloat("Spring Strength", &settings_.springStrength, 0.001f, 0.1f);
        ImGui::SliderFloat("Rest Length", &settings_.restLength, 50.0f, 400.0f);
        ImGui::SliderFloat("Damping", &settings_.damping, 0.5f, 0.99f);
        ImGui::SliderFloat("Centering", &settings_.centeringStrength, 0.0f, 0.05f);

        ImGui::Spacing();
        ImGui::Text("Rendering");
        ImGui::SliderFloat("LOD Zoom Threshold", &settings_.lodZoomThreshold, 0.1f, 1.0f);

        ImGui::Spacing();
        if (ImGui::Button("Save Settings")) {
            saveSettingsRequested_ = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Project Settings");

        int depth = currentNeighborhoodDepth;
        if (ImGui::SliderInt("Neighborhood Depth", &depth, 0, 10)) {
            neighborhoodDepthChange_ = depth;
        }

        int maxNodes = static_cast<int>(currentMaxVisibleNodes);
        if (ImGui::SliderInt("Max Visible Nodes", &maxNodes, 10, 2000)) {
            maxVisibleNodesChange_ = static_cast<size_t>(maxNodes);
        }
    }

    ImGui::End();
}

bool Window::consumeSaveSettingsRequest() {
    bool result = saveSettingsRequested_;
    saveSettingsRequested_ = false;

    return result;
}

std::optional<int> Window::consumeNeighborhoodDepthChange() {
    auto result = neighborhoodDepthChange_;
    neighborhoodDepthChange_.reset();

    return result;
}

std::optional<size_t> Window::consumeMaxVisibleNodesChange() {
    auto result = maxVisibleNodesChange_;
    maxVisibleNodesChange_.reset();

    return result;
}

// --- Person Modal ---

void Window::renderCreationModals(const std::vector<gedcomx::SourceDescription>& knownSources) {
    renderPersonEditor(knownSources);
    renderNewRelationshipModal();
    renderExportModal(knownSources);
    renderLinkPersonModal();
}

/*
void Window::renderNewSourceModal() {
    if (showNewSourceModal_ && !ImGui::IsPopupOpen("New Source")) {
        ImGui::OpenPopup("New Source");
    }

    if (ImGui::BeginPopupModal("New Source", &showNewSourceModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Title", newSourceTitleBuffer_, sizeof(newSourceTitleBuffer_));

        bool titleEmpty = newSourceTitleBuffer_[0] == '\0';
        ImGui::BeginDisabled(titleEmpty);
        if (ImGui::Button("Create Source")) {
            gedcomx::SourceDescription src;
            src.id = idgen::generateLocalId("S_");
            src.titles.push_back({ "", newSourceTitleBuffer_ });
            newSourceResult_ = src;
            pendingAutoSelectSourceId_ = src.id;

            newSourceTitleBuffer_[0] = '\0';
            ImGui::CloseCurrentPopup();
            showNewSourceModal_ = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            showNewSourceModal_ = false;
        }

        ImGui::EndPopup();
    }
}
*/

/*
void Window::renderNewPersonModal(const std::vector<gedcomx::SourceDescription>& knownSources) {
    if (showNewPersonModal_ && !ImGui::IsPopupOpen("New Person")) {
        ImGui::OpenPopup("New Person");
    }

    if (ImGui::BeginPopupModal("New Person", &showNewPersonModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        // If a source was just created from the next popup below, wait for
        // it to show up in knownSources (App processes the request a frame
        // later) and auto-select it the moment it appears.
        if (!pendingAutoSelectSourceId_.empty()) {
            for (int i = 0; i < (int)knownSources.size(); ++i) {
                if (knownSources[i].id == pendingAutoSelectSourceId_) {
                    selectedSourceIndex_ = i;
                    pendingAutoSelectSourceId_.clear();
                    break;
                }
            }
        }

        ImGui::InputText("Given Name", newPersonGiveNameBuffer_, sizeof(newPersonGiveNameBuffer_));
        ImGui::InputText("Surname", newPersonSurnameBuffer_, sizeof(newPersonSurnameBuffer_));

        ImGui::Spacing();
        ImGui::Text("Source");

        if (ImGui::BeginCombo("##sourceCombo", selectedSourceIndex_ >= 0 && selectedSourceIndex_ < (int)knownSources.size() ? knownSources[selectedSourceIndex_].primaryTitle().c_str() : "Select a source...")) {
            renderSelectableList<gedcomx::SourceDescription>(
                knownSources, &selectedSourceIndex_,
                [](const gedcomx::SourceDescription& s) { return s.primaryTitle(); },
                [](const gedcomx::SourceDescription& s) { return s.id; }
            );

            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("+ New Source")) {
            ImGui::OpenPopup("New Source");
        }
        
        // The nested popup
        if (ImGui::BeginPopupModal("New Source", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Title", newSourceTitleBuffer_, sizeof(newSourceTitleBuffer_));

            bool titleEmpty = newSourceTitleBuffer_[0] == '\0';
            ImGui::BeginDisabled(titleEmpty);
            if (ImGui::Button("Create Source")) {
                gedcomx::SourceDescription src;
                src.id = idgen::generateLocalId("S_");
                src.titles.push_back({ "", newSourceTitleBuffer_ });
                newSourceResult_ = src;
                pendingAutoSelectSourceId_ = src.id;

                newSourceTitleBuffer_[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
                showNewSourceModal_ = false;
            }

            ImGui::EndPopup();
        }

        bool nameEmpty = newPersonGiveNameBuffer_[0] == '\0' && newPersonSurnameBuffer_[0] == '\0';
        bool noSourceSelected = selectedSourceIndex_ < 0 || selectedSourceIndex_ >= (int)knownSources.size();

        ImGui::Spacing();
        ImGui::BeginDisabled(nameEmpty || noSourceSelected);
        if (ImGui::Button("Create Person")) {
            gedcomx::Person person;
            person.id = idgen::generateLocalId("P_");
            person.sourceId = knownSources[selectedSourceIndex_].id;

            gedcomx::NameForm form;
            std::string given = newPersonGiveNameBuffer_;
            std::string surname = newPersonSurnameBuffer_;
            form.fullText = given + (given.empty() || surname.empty() ? "" : " ") + surname;
            if (!given.empty()) form.parts.push_back({ given, "http://gedcomx.org/Given" });
            if (!surname.empty()) form.parts.push_back({ surname, "http://gedcomx.org/Surname" });

            gedcomx::Name name;
            name.nameForms.push_back(form);
            person.names.push_back(name);

            newPersonResult_ = person;

            newPersonGiveNameBuffer_[0] = '\0';
            newPersonSurnameBuffer_[0] = '\0';
            selectedSourceIndex_ = -1;
            ImGui::CloseCurrentPopup();
            showNewPersonModal_ = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            showNewPersonModal_ = false;
        }

        ImGui::EndPopup();
    }
}
*/

std::optional<gedcomx::SourceDescription> Window::consumeNewSourceRequest() {
    auto result = newSourceResult_;
    newSourceResult_.reset();

    return result;
}

/*
std::optional<gedcomx::Person> Window::consumeNewPersonRequest() {
    auto result = newPersonResult_;
    newPersonResult_.reset();

    return result;
}
*/

// --- Relationship Modal ---

void Window::renderNewRelationshipModal() {
    if (showNewRelationshipModal_ && !ImGui::IsPopupOpen("New Relationship")) {
        ImGui::OpenPopup("New Relationship");
    }

    if (ImGui::BeginPopupModal("New Relationship", &showNewRelationshipModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTabBar("RelationshipEditorTabs")) {
            if (ImGui::BeginTabItem("People")) {
                renderRelationshipEditorPeopleTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Details")) {
                renderRelationshipEditorDetailsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();

        bool noPerson1 = selectedPersonIndex_[0] < 0 || selectedPersonIndex_[0] >= (int)personSearchResults_[0].size();
        bool noPerson2 = selectedPersonIndex_[1] < 0 || selectedPersonIndex_[1] >= (int)personSearchResults_[1].size();
        bool isCustomType = selectedRelationshipTypeIndex_ == IM_ARRAYSIZE(kRelationshipTypes) - 1;
        bool noCustomType = isCustomType && relationshiptypeCustomBuffer_[0] == '\0';

        ImGui::BeginDisabled(noPerson1 || noPerson2 || noCustomType);
        if (ImGui::Button("Create Relationship")) {
            gedcomx::Relationship rel;
            rel.id = idgen::generateLocalId("R_");

            const std::string& p1RowKey = personSearchResults_[0][selectedPersonIndex_[0]].id;
            const std::string& p2RowKey = personSearchResults_[1][selectedPersonIndex_[1]].id;
            rel.person1Id = storage::stripRowKeyPrefix(p1RowKey, relationshipSourceId_);
            rel.person2Id = storage::stripRowKeyPrefix(p2RowKey, relationshipSourceId_);

            rel.type = isCustomType ? relationshiptypeCustomBuffer_ : kRelationshipTypes[selectedRelationshipTypeIndex_];

            newRelationshipResult_ = std::make_pair(rel, relationshipSourceId_);

            personSearchBuffer_[0][0] = personSearchBuffer_[1][0] = '\0';
            personSearchResults_[0].clear();
            personSearchResults_[1].clear();
            selectedPersonIndex_[0] = selectedPersonIndex_[1] = -1;
            relationshipSourceId_.clear();
            relationshiptypeCustomBuffer_[0] = '\0';
            selectedRelationshipTypeIndex_ = 0;

            ImGui::CloseCurrentPopup();
            showNewRelationshipModal_ = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            showNewRelationshipModal_ = false;
        }

        ImGui::EndPopup();
    }
}

void Window::renderRelationshipEditorPeopleTab() {
    ImGui::PushID(0);
    ImGui::Text("Person 1");
    if (ImGui::InputText("Search", personSearchBuffer_[0], sizeof(personSearchBuffer_[0]))) {
        pendingSearchQuery_[0] = personSearchBuffer_[0];
        selectedPersonIndex_[0] = -1;
    }
    for (int i = 0; i < (int)personSearchResults_[0].size(); ++i) {
        bool isSelected = (i == selectedPersonIndex_[0]);
        std::string label = personSearchResults_[0][i].primaryFullText() + "##" + personSearchResults_[0][i].id;
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            selectedPersonIndex_[0] = i;
            relationshipSourceId_ = personSearchResults_[0][i].sourceId;

            personSearchBuffer_[1][0] = '\0';
            personSearchResults_[1].clear();
            selectedPersonIndex_[1] = -1;
        }
    }
    ImGui::PopID();
    ImGui::Spacing();

    ImGui::PushID(1);
    ImGui::Text("Person 2");
    ImGui::BeginDisabled(relationshipSourceId_.empty());
    if (relationshipSourceId_.empty()) {
        ImGui::TextDisabled("(select Person 1 first)");
    }
    else {
        ImGui::TextDisabled("Searching within: %s", relationshipSourceId_.c_str());
        if (ImGui::InputText("Search", personSearchBuffer_[1], sizeof(personSearchBuffer_[1]))) {
            pendingSearchQuery_[1] = personSearchBuffer_[1];
            selectedPersonIndex_[1] = -1;
        }
        renderSelectableList<gedcomx::Person>(
            personSearchResults_[1], &selectedPersonIndex_[1],
            [](const gedcomx::Person& p) { return p.primaryFullText(); },
            [](const gedcomx::Person& p) { return p.id; }
        );
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void Window::renderRelationshipEditorDetailsTab() {
    ImGui::Text("Relationship Type");
    if (ImGui::BeginCombo("##relType", selectedRelationshipTypeIndex_ >= 0 && selectedRelationshipTypeIndex_ < IM_ARRAYSIZE(kRelationshipTypes)
        ? gedcomx::formatTypeUri(kRelationshipTypes[selectedRelationshipTypeIndex_]).c_str() : "")) {
        for (int i = 0; i < IM_ARRAYSIZE(kRelationshipTypes); ++i) {
            std::string label = gedcomx::formatTypeUri(kRelationshipTypes[i]);
            if (ImGui::Selectable(label.c_str(), i == selectedRelationshipTypeIndex_)) selectedRelationshipTypeIndex_ = i;
        }
        ImGui::EndCombo();
    }

    bool isCustomType = selectedRelationshipTypeIndex_ == IM_ARRAYSIZE(kRelationshipTypes) - 1;
    if (isCustomType) {
        ImGui::InputText("Custom Type URI", relationshiptypeCustomBuffer_, sizeof(relationshiptypeCustomBuffer_));
    }
}

std::optional<std::pair<gedcomx::Relationship, std::string>> Window::consumeNewRelationshipRequest() {
    auto result = newRelationshipResult_;
    newRelationshipResult_.reset();

    return result;
}

std::optional<std::pair<std::string, std::string>> Window::consumePersonSearchQuery(int pickerIndex) {
    if (!pendingSearchQuery_[pickerIndex]) return std::nullopt;
    std::string query = *pendingSearchQuery_[pickerIndex];
    pendingSearchQuery_[pickerIndex].reset();

    // Picker 0 (Person 1) is unrestricted; picker 1 (Person 2) is scoped to
    // whichever source Person 1 belongs to.
    std::string sourceFilter = (pickerIndex == 1) ? relationshipSourceId_ : "";
    return std::make_pair(query, sourceFilter);
}

void Window::setPersonSearchResults(int pickerIndex, const std::vector<gedcomx::Person>& results) {
    personSearchResults_[pickerIndex] = results;
}

/*
std::optional<std::pair<gedcomx::Person, std::string>> Window::consumeNewFactRequest() {
    auto result = newFactResult_;
    newFactResult_.reset();

    return result;
}
*/

// --- Export Modal ---

void Window::renderExportModal(const std::vector<gedcomx::SourceDescription>& knownSources) {
    if (showExportModal_ && !ImGui::IsPopupOpen("Export Source")) {
        ImGui::OpenPopup("Export Source");
    }

    if (ImGui::BeginPopupModal("Export Source", &showExportModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Choose a source to export:");

        if (ImGui::BeginCombo("##exportSourceCombo",
            selectedExportSourceIndex_ >= 0 && selectedExportSourceIndex_ < (int)knownSources.size()
            ? knownSources[selectedExportSourceIndex_].primaryTitle().c_str() : "Select a source...")) {
            renderSelectableList < gedcomx::SourceDescription>(
                knownSources, &selectedExportSourceIndex_,
                [](const gedcomx::SourceDescription& s) { return s.primaryTitle(); },
                [](const gedcomx::SourceDescription& s) { return s.id; }
            );
            ImGui::EndCombo();
        }

        bool noSourceSelected = selectedExportSourceIndex_ < 0 || selectedExportSourceIndex_ >= (int)knownSources.size();

        ImGui::Spacing();
        ImGui::BeginDisabled(noSourceSelected);
        if (ImGui::Button("Export...")) {
            exportSourceResult_ = knownSources[selectedExportSourceIndex_].id;
            selectedExportSourceIndex_ = -1;
            ImGui::CloseCurrentPopup();
            showExportModal_ = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            showExportModal_ = false;
        }

        ImGui::EndPopup();
    }
}

std::optional<std::string> Window::consumeExportSourceRequest() {
    auto result = exportSourceResult_;
    exportSourceResult_.reset();

    return result;
}

void Window::requestExportSaveDialog(const std::string& suggestedFileName) {
    SDL_DialogFileFilter filters[] = { {"GEDCOM X (JSON)", "json"} };
    SDL_ShowSaveFileDialog(&Window::handleExportSaveDialogResult, this, window_, filters, 1, suggestedFileName.c_str());
}

void Window::handleExportSaveDialogResult(void* userdata, const char* const* filelist, int filter) {
    Window* self = static_cast<Window*>(userdata);
    if (!filelist || !filelist[0]) return;

    std::lock_guard<std::mutex> lock(self->exportSaveMutex_);
    self->pendingExportSavePath_ = filelist[0];
}

std::optional<std::string> Window::consumeExportSavePath() {
    std::lock_guard<std::mutex> lock(exportSaveMutex_);
    std::optional<std::string> result = pendingExportSavePath_;
    pendingExportSavePath_.reset();

    return result;
}

// --- Link Person Modal ---

void Window::renderLinkPersonModal() {
    if (showLinkPersonModal_ && !ImGui::IsPopupOpen("Link Person")) {
        ImGui::OpenPopup("Link Person");
    }

    if (ImGui::BeginPopupModal("Link Person", &showLinkPersonModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Link record: %s", linkSourcePersonKey_.c_str());
        ImGui::Separator();
        ImGui::Text("Search for the matching person in another source:");

        if (ImGui::InputText("Search", linkSearchBuffer_, sizeof(linkSearchBuffer_))) {
            pendingLinkSearchQuery_ = linkSearchBuffer_;
            selectedLinkIndex_ = -1;
        }

        // Filter out Person 1 so you can't link to it.
        std::vector<gedcomx::Person> filteredLinkResults;
        for (const auto& p : linkSearchResults_) {
            if (p.id != linkSourcePersonKey_) filteredLinkResults.push_back(p);
        }
        renderSelectableList<gedcomx::Person>(
            filteredLinkResults, &selectedLinkIndex_,
            [](const gedcomx::Person& p) { return p.primaryFullText(); },
            [](const gedcomx::Person& p) { return p.id; }
        );

        bool noneSelected = selectedLinkIndex_ < 0 || selectedLinkIndex_ >= (int)linkSearchResults_.size();

        ImGui::Spacing();
        ImGui::BeginDisabled(noneSelected);
        if (ImGui::Button("Link")) {
            linkPersonResult_ = std::make_pair(linkSourcePersonKey_, linkSearchResults_[selectedLinkIndex_].id);

            linkSearchBuffer_[0] = '\0';
            linkSearchResults_.clear();
            selectedLinkIndex_ = -1;
            ImGui::CloseCurrentPopup();
            showLinkPersonModal_ = false;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            showLinkPersonModal_ = false;
        }

        ImGui::EndPopup();
    }
}

std::optional<std::pair<std::string, std::string>> Window::consumeLinkPersonRequest() {
    auto result = linkPersonResult_;
    linkPersonResult_.reset();

    return result;
}

std::optional<std::string> Window::consumeLinkSearchQuery() {
    auto result = pendingLinkSearchQuery_;
    pendingLinkSearchQuery_.reset();

    return result;
}

void Window::setLinkSearchResults(const std::vector<gedcomx::Person>& results) {
    linkSearchResults_ = results;
}