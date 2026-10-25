#include "window.h"
#include "id_generator.h"
#include "storage/database.h"

namespace {
    const char* kRelFactTypes[] = {
        "http://gedcomx.org/Marriage",
        "http://gedcomx.org/CommonLawMarriage",
        "http://gedcomx.org/CivilUnion",
        "http://gedcomx.org/DomesticPartnership",
        "http://gedcomx.org/Divorce",
        "http://gedcomx.org/Annulment",
        "http://gedcomx.org/Separation",
        "Custom..."
    };                              

    bool isCustomFactType(const std::string& type) {
        return type.rfind("http://gedcomx.org/", 0) != 0;  // doesn't start with the standard prefix
    }
    
    const gedcomx::Fact* findUnionFact(const gedcomx::Relationship& rel) {
        const gedcomx::Fact* custom = nullptr;
        for (const auto& f : rel.facts) {
            for (const char* t : kRelFactTypes) {
                if (f.type == t) return &f;  // standard type wins immediately
            }
            if (!custom && isCustomFactType(f.type)) custom = &f;
        }
        return custom;
    }

    // Read-only Name / Birth / Death summary for one picker slot.
    // `p` points into personSearchResults_, so use it within this frame only.
    void renderPersonSummary(const gedcomx::Person* p) {
        if (!p) {
            ImGui::TextDisabled("(no one selected)");
            return;
        }
        const gedcomx::Fact* birth = p->primaryBirthFact();
        const gedcomx::Fact* death = p->primaryDeathFact();

        ImGui::Text("%s", p->primaryFullText().c_str());
        ImGui::TextDisabled("Born: %s", birth && !birth->date.original.empty() ? birth->date.original.c_str() : "-");
        ImGui::TextDisabled("Died: %s", death && !death->date.original.empty() ? death->date.original.c_str() : "-");
    }
}

void Window::renderRelationshipEditorTopStrip(const std::vector<gedcomx::SourceDescription>& knownSources) {
    auto selected = [&](int slot) -> const gedcomx::Person* {
        int i = selectedPersonIndex_[slot];
        return (i >= 0 && i < (int)personSearchResults_[slot].size()) ? &personSearchResults_[slot][i] : nullptr;
    };

    if (editingExistingRelationship_) {
        if (ImGui::BeginTable("RelationshipPeople", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("##p1", ImGuiTableColumnFlags_WidthFixed, 280.0f);
            ImGui::TableSetupColumn("##p2", ImGuiTableColumnFlags_WidthFixed, 280.0f);
            for (int slot = 0; slot < 2; ++slot) {
                ImGui::TableNextColumn();
                ImGui::Text("Person %d", slot + 1);
                ImGui::Separator();
                renderPersonSummary(selected(slot));
            }
            ImGui::EndTable();
        }
        const gedcomx::Fact* unionFact = findUnionFact(workingRelationship_);
        ImGui::Text("Union:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", unionFact ? gedcomx::formatTypeUri(unionFact->type).c_str() : "(none)");
        return;
    }

    if (ImGui::BeginTable("RelationshipPeople", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##p1", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableSetupColumn("##p2", ImGuiTableColumnFlags_WidthFixed, 280.0f);

        // --- Person 1 ---
        ImGui::TableNextColumn();
        ImGui::PushID(0);
        ImGui::Text("Person 1");
        if (ImGui::InputText("Search", personSearchBuffer_[0], sizeof(personSearchBuffer_[0]))) {
            pendingSearchQuery_[0] = personSearchBuffer_[0];
            selectedPersonIndex_[0] = -1;
        }
        int before = selectedPersonIndex_[0];
        if (selectedPersonIndex_[0] < 0) {
            renderSelectableList<gedcomx::Person>(
                personSearchResults_[0], &selectedPersonIndex_[0],
                [](const gedcomx::Person& p) { return p.primaryFullText(); },
                [](const gedcomx::Person& p) { return p.id; }
            );
        }
        if (selectedPersonIndex_[0] != before && selectedPersonIndex_[0] >= 0) {
            // Person 1 locks the source; Person 2 has to start over within it.
            relationshipSourceId_ = personSearchResults_[0][selectedPersonIndex_[0]].sourceId;
            personSearchBuffer_[1][0] = '\0';
            personSearchResults_[1].clear();
            selectedPersonIndex_[1] = -1;
        }
        ImGui::Separator();
        renderPersonSummary(selected(0));
        ImGui::PopID();

        // --- Person 2 ---
        ImGui::TableNextColumn();
        ImGui::PushID(1);
        ImGui::Text("Person 2");
        ImGui::BeginDisabled(relationshipSourceId_.empty());
        if (relationshipSourceId_.empty()) {
            ImGui::TextDisabled("(select Person 1 first)");
        }
        else {
            std::string sourceLabel = relationshipSourceId_;  // fallback if not found
            for (const auto& s : knownSources) {
                if (s.id == relationshipSourceId_) {
                    sourceLabel = s.primaryTitle();
                    break;
                }
            }
            ImGui::TextDisabled("Searching within: %s", sourceLabel.c_str());
            if (ImGui::InputText("Search", personSearchBuffer_[1], sizeof(personSearchBuffer_[1]))) {
                pendingSearchQuery_[1] = personSearchBuffer_[1];
                selectedPersonIndex_[1] = -1;
            }
            if (selectedPersonIndex_[1] < 0) {
                renderSelectableList<gedcomx::Person>(
                    personSearchResults_[1], &selectedPersonIndex_[1],
                    [](const gedcomx::Person& p) { return p.primaryFullText(); },
                    [](const gedcomx::Person& p) { return p.id; }
                );
            }
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        renderPersonSummary(selected(1));

        // --- Marriage Fact ---
        const gedcomx::Fact* unionFact = findUnionFact(workingRelationship_);
        ImGui::Text("Union:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", unionFact ? gedcomx::formatTypeUri(unionFact->type).c_str() : "(none)");

        ImGui::PopID();

        ImGui::EndTable();
    }
}

void Window::renderRelationshipEditorChildrenTab() {}

void Window::renderRelationshipEditorEventsTab() {
    constexpr int kCount = IM_ARRAYSIZE(kRelFactTypes);

    auto resetEventForm = [&] {
        relEditingFactIndex_ = -1;
        relSelectedFactTypeIndex_ = 0;
        relFactTypeCustomBuffer_[0] = relFactDateBuffer_[0] = relFactPlaceBuffer_[0] = '\0';
    };

    for (int i = 0; i < (int)workingRelationship_.facts.size(); ++i) {
        ImGui::PushID(i);
        const auto& fact = workingRelationship_.facts[i];
        ImGui::BulletText("%s: %s %s", gedcomx::formatTypeUri(fact.type).c_str(), fact.date.original.c_str(), fact.place.original.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit")) {
            relEditingFactIndex_ = i;
            relSelectedFactTypeIndex_ = kCount - 1;
            for (int t = 0; t < kCount - 1; ++t) {
                if (fact.type == kRelFactTypes[t]) {
                    relSelectedFactTypeIndex_ = t;
                    break;
                }
            }
            strncpy_s(relFactTypeCustomBuffer_, fact.type.c_str(), sizeof(relFactTypeCustomBuffer_) - 1);
            strncpy_s(relFactDateBuffer_, fact.date.original.c_str(), sizeof(relFactDateBuffer_) - 1);
            strncpy_s(relFactPlaceBuffer_, fact.place.original.c_str(), sizeof(relFactPlaceBuffer_) - 1);
        }
        ImGui::SameLine();
        bool removed = false;
        if (ImGui::SmallButton("Remove")) {
            workingRelationship_.facts.erase(workingRelationship_.facts.begin() + i);
            if (relEditingFactIndex_ == i) relEditingFactIndex_ = -1;
            else if (relEditingFactIndex_ > i) --relEditingFactIndex_;
            removed = true;
        }
        ImGui::PopID();
        if (removed) break;
    }

    ImGui::Separator();
    ImGui::Text(relEditingFactIndex_ >= 0 ? "Edit Event" : "Add Event");
    if (ImGui::BeginCombo("Type", relSelectedFactTypeIndex_ >= 0 && relSelectedFactTypeIndex_ < kCount ? gedcomx::formatTypeUri(kRelFactTypes[relSelectedFactTypeIndex_]).c_str() : "")) {
        for (int i = 0; i < kCount; ++i) {
            std::string label = gedcomx::formatTypeUri(kRelFactTypes[i]);
            if (ImGui::Selectable(label.c_str(), i == relSelectedFactTypeIndex_)) relSelectedFactTypeIndex_ = i;
        }
        ImGui::EndCombo();
    }
    bool isCustomType = relSelectedFactTypeIndex_ == kCount - 1;
    if (isCustomType)
        ImGui::InputText("Custom Type URI", relFactTypeCustomBuffer_, sizeof(relFactTypeCustomBuffer_));
    ImGui::InputText("Date", relFactDateBuffer_, sizeof(relFactDateBuffer_));
    ImGui::InputText("Place", relFactPlaceBuffer_, sizeof(relFactPlaceBuffer_));

    bool noCustomType = isCustomType && relFactTypeCustomBuffer_[0] == '\0';
    ImGui::BeginDisabled(noCustomType);
    if (ImGui::Button(relEditingFactIndex_ >= 0 ? "Update Event" : "Add Event")) {
        gedcomx::Fact fact;
        fact.type = isCustomType ? relFactTypeCustomBuffer_ : kRelFactTypes[relSelectedFactTypeIndex_];
        fact.date.original = relFactDateBuffer_;
        fact.place.original = relFactPlaceBuffer_;

        if (relEditingFactIndex_ >= 0) {
            fact.id = workingRelationship_.facts[relEditingFactIndex_].id;
            fact.conclusion = workingRelationship_.facts[relEditingFactIndex_].conclusion;
            workingRelationship_.facts[relEditingFactIndex_] = fact;
        }
        else {
            fact.id = idgen::generateLocalId("F_");
            workingRelationship_.facts.push_back(fact);
        }
        resetEventForm();
    }
    ImGui::EndDisabled();

    if (relEditingFactIndex_ >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel Edit")) resetEventForm();
    }
}

void Window::renderRelationshipEditorCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources) {}

void Window::renderRelationshipEditorAttributesTab() {}

void Window::requestNewRelationshipEditor() {
    workingRelationship_ = gedcomx::Relationship{};

    for (int i = 0; i < 2; ++i) {
        personSearchBuffer_[i][0] = '\0';
        personSearchResults_[i].clear();
        selectedPersonIndex_[i] = -1;
        pendingSearchQuery_[i].reset();
    }
    relationshipSourceId_.clear();

    showNewRelationshipModal_ = true;
}

void Window::requestEditRelationshipEditor(const gedcomx::Relationship& rel, const gedcomx::Person& p1, const gedcomx::Person& p2) {
    workingRelationship_ = rel;
    editingExistingRelationship_ = true;
    relationshipSourceId_ = rel.sourceId;

    const gedcomx::Person* people[2] = { &p1, &p2 };
    for (int i = 0; i < 2; ++i) {
        personSearchBuffer_[i][0] = '\0';
        personSearchResults_[i] = { *people[i] };
        selectedPersonIndex_[i] = 0;
        pendingSearchQuery_[i].reset();
    }
    editingExistingRelationship_ = false;
    relEditingFactIndex_ = -1;
    relSelectedFactTypeIndex_ = 0;
    relFactTypeCustomBuffer_[0] = relFactDateBuffer_[0] = relFactPlaceBuffer_[0] = '\0';

    showNewRelationshipModal_ = true;
}

void Window::renderRelationshipEditor(const std::vector<gedcomx::SourceDescription>& knownSources) {
    if (showNewRelationshipModal_ && !ImGui::IsPopupOpen("Relationship Editor")) {
        ImGui::OpenPopup("Relationship Editor");
    }

    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Relationship Editor", &showNewRelationshipModal_)) {
        renderRelationshipEditorTopStrip(knownSources);
        ImGui::Separator();

        // --- tab bar ---

        if (ImGui::BeginTabBar("RelationshipEditorTabs")) {
            if (ImGui::BeginTabItem("Children")) {
                renderRelationshipEditorChildrenTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Events")) {
                renderRelationshipEditorEventsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Citations")) {
                renderRelationshipEditorCitationsTab(knownSources);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Attributes")) {
                renderRelationshipEditorAttributesTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        bool noPerson1 = selectedPersonIndex_[0] < 0 || selectedPersonIndex_[0] >= (int)personSearchResults_[0].size();
        bool noPerson2 = selectedPersonIndex_[1] < 0 || selectedPersonIndex_[1] >= (int)personSearchResults_[1].size();

        ImGui::BeginDisabled(noPerson1 || noPerson2);
        if (ImGui::Button("Save")) {
            gedcomx::Relationship result = workingRelationship_;
            if (!editingExistingRelationship_) result.id = idgen::generateLocalId("R_");
            result.type = "http://gedcomx.org/Couple";
            result.person1Id = storage::stripRowKeyPrefix(personSearchResults_[0][selectedPersonIndex_[0]].id, relationshipSourceId_);
            result.person2Id = storage::stripRowKeyPrefix(personSearchResults_[1][selectedPersonIndex_[1]].id, relationshipSourceId_);

            if (editingExistingRelationship_)
                editRelationshipResult_ = std::make_pair(result, relationshipSourceId_);
            else
                newRelationshipResult_ = std::make_pair(std::vector<gedcomx::Relationship>{ result }, relationshipSourceId_);

            showNewRelationshipModal_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showNewRelationshipModal_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

std::optional<std::pair<std::vector<gedcomx::Relationship>, std::string>> Window::consumeNewRelationshipRequest() {
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

std::optional<std::pair<gedcomx::Relationship, std::string>> Window::consumeRelationshipEditResult() {
    auto result = editRelationshipResult_;
    editRelationshipResult_.reset();
    return result;
}

void Window::setPersonSearchResults(int pickerIndex, const std::vector<gedcomx::Person>& results) {
    personSearchResults_[pickerIndex] = results;
}