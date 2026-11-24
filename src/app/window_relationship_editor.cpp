#include <algorithm>

#include "window.h"
#include "id_generator.h"
#include "storage/database.h"
#include "gedcomx/parent_child.h"

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

    struct ParentageOption {
        const char* label;
        const char* typeUri;  // typeUri == nullptr -> no fact stored
    };

    constexpr int kNotAParent = 0;
    constexpr int kUnspecified = 1;

    const ParentageOption kParentage[] = {
        { "Not a parent", nullptr },
        { "Unspecified", nullptr },
        { "Biological", gedcomx::kBiologicalParent },
        { "Adoptive", gedcomx::kAdoptiveParent },
        { "Step", gedcomx::kStepParent },
        { "Foster", gedcomx::kFosterParent },
        { "Guardian", gedcomx::kGuardianParent },
    };
    constexpr int kParentageCount = IM_ARRAYSIZE(kParentage);

    // Which picker entry does an existing link correspond to? No parentage fact = Unspecified.
    int parentageIndexFor(const gedcomx::Relationship& link) {
        for (const auto& f : link.facts)
            for (int i = 2; i < kParentageCount; ++i)
                if (f.type == kParentage[i].typeUri) return i;
        return kUnspecified;
    }
}

void Window::resetRelFactForm() {
    relEditingFactIndex_ = -1;
    relSelectedFactTypeIndex_ = 0;
    relFactTypeCustomBuffer_[0] = relFactDateBuffer_[0] = relFactPlaceBuffer_[0] = '\0';
    relFactDate_ = gedcomx::Date{};
    relFactPlaceId_.clear();
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

void Window::renderRelationshipEditorChildrenTab() {
    auto selected = [&](int slot) -> const gedcomx::Person* {
        int i = selectedPersonIndex_[slot];
        return (i >= 0 && i < (int)personSearchResults_[slot].size()) ? &personSearchResults_[slot][i] : nullptr;
    };
    const gedcomx::Person* p1 = selected(0);
    const gedcomx::Person* p2 = selected(1);
    if (!p1 || !p2) {
        ImGui::TextDisabled("Select both people first.");
        return;
    }
    renderChildrenEditorBody(p1, p2);
}

void Window::renderChildrenEditorBody(const gedcomx::Person* parent1, const gedcomx::Person* parent2) {
    const gedcomx::Person* parents[2] = { parent1, parent2 };
    const int slotCount = parent2 ? 2 : 1;

    // --- add a child ---
    if (ImGui::InputText("Add child", personSearchBuffer_[2], sizeof(personSearchBuffer_[2]))) {
        pendingSearchQuery_[2] = personSearchBuffer_[2];
    }
    ImGui::SameLine();
    if (ImGui::Button("+ New person...")) requestNewChildPersonEditor();

    int pick = -1;
    for (int i = 0; i < (int)personSearchResults_[2].size(); ++i) {
        const auto& cand = personSearchResults_[2][i];
        if (cand.id == parents[0]->id || (parents[1] && cand.id == parents[1]->id)) continue;  // cannot be your own parent
        bool alreadyStaged = std::any_of(stagedChildren_.begin(), stagedChildren_.end(),
            [&](const StagedChild& c) {
                return c.person.id == cand.id;
            });
        if (alreadyStaged) continue;

        std::string label = cand.primaryFullText() + "##" + cand.id;
        if (ImGui::Selectable(label.c_str())) pick = i;
    }
    if (personSearchBuffer_[2][0] != '\0' && personSearchResults_[2].empty()) {
        ImGui::TextDisabled("No matching people in this source.");
    }
    if (pick >= 0) {
        StagedChild sc;
        sc.person = personSearchResults_[2][pick];
        stagedChildren_.push_back(std::move(sc));
        personSearchBuffer_[2][0] = '\0';
        personSearchResults_[2].clear();
    }

    // --- staged children ---
    ImGui::Separator();
    if (stagedChildren_.empty()) ImGui::TextDisabled("No children");

    int removeIndex = -1;
    for (int i = 0; i < (int)stagedChildren_.size(); ++i) {
        auto& c = stagedChildren_[i];
        ImGui::PushID(i);

        ImGui::Text("%s", c.person.primaryFullText().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) removeIndex = i;

        ImGui::Indent();
        bool noLink = true;
        for (int slot = 0; slot < slotCount; ++slot) {
            ImGui::PushID(slot);
            ImGui::Text("%s:", parents[slot]->primaryFullText().c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("##parentage", kParentage[c.parentage[slot]].label)) {
                for (int o = 0; o < kParentageCount; ++o) {
                    if (ImGui::Selectable(kParentage[o].label, o == c.parentage[slot])) c.parentage[slot] = o;
                }
                ImGui::EndCombo();
            }
            if (c.parentage[slot] != kNotAParent) noLink = false;
            ImGui::PopID();
        }
        if (noLink) {
            ImGui::TextDisabled(c.isNew ? "(will be created without a parent link)" : "(no link will be saved for this person)");
        }
        ImGui::Unindent();

        ImGui::PopID();
    }

    if (removeIndex >= 0) {
        for (int slot = 0; slot < 2; ++slot) {
            const auto& link = stagedChildren_[removeIndex].link[slot];
            if (link) removedChildLinks_.push_back(*link);
        }
        stagedChildren_.erase(stagedChildren_.begin() + removeIndex);
    }
}

std::vector<gedcomx::ChildLinkInput> Window::collectChildInputs(int slotCount, std::vector<gedcomx::Person>& newPersons) const {
    std::vector<gedcomx::ChildLinkInput> inputs;
    for (const auto& c : stagedChildren_) {
        gedcomx::ChildLinkInput in;
        in.childId = storage::stripRowKeyPrefix(c.person.id, relationshipSourceId_);
        if (c.isNew) {
            gedcomx::Person np = c.person;
            np.id = in.childId;  // local id for save()
            np.sourceId = relationshipSourceId_;
            newPersons.push_back(std::move(np));
        }
        for (int s = 0; s < slotCount; ++s) {
            const char* uri = kParentage[c.parentage[s]].typeUri;
            in.parent[s].isParent = (c.parentage[s] != kNotAParent);
            in.parent[s].parentageType = uri ? uri : "";
            in.parent[s].existingLink = c.link[s];
        }
        inputs.push_back(std::move(in));
    }
    return inputs;
}

void Window::renderRelationshipEditorEventsTab() {
    constexpr int kCount = IM_ARRAYSIZE(kRelFactTypes);

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
            relFactDate_ = fact.date;  // the whole date: formal form, calendar, quality and sort value
            relFactPlaceId_ = fact.place.descriptionId;
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

    if (renderDateInput("relFactDate", relFactDate_, relFactDateBuffer_, sizeof(relFactDateBuffer_), ImGui::CalcItemWidth()))
        openDateEditor(DateEditorTarget::RelationshipFact, relFactDate_);
    ImGui::SameLine();
    ImGui::TextUnformatted("Date");

    ImGui::InputText("Place", relFactPlaceBuffer_, sizeof(relFactTypeCustomBuffer_));  // what the source says
    if (renderPlacePicker("##relFactPlace", relFactPlaceId_, nullptr, ImGui::CalcItemWidth() - 60.0f)) {
        if (relFactPlaceBuffer_[0] == '\0')
            strncpy_s(relFactPlaceBuffer_, placeTitleById(relFactPlaceId_).c_str(), _TRUNCATE);
    }
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::BeginDisabled(relFactPlaceId_.empty());
    if (ImGui::SmallButton("Unlink")) relFactPlaceId_.clear();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted("Linked place");

    bool noCustomType = isCustomType && relFactTypeCustomBuffer_[0] == '\0';
    ImGui::BeginDisabled(noCustomType);
    if (ImGui::Button(relEditingFactIndex_ >= 0 ? "Update Event" : "Add Event")) {
        gedcomx::Fact fact = relEditingFactIndex_ >= 0 ? workingRelationship_.facts[relEditingFactIndex_] : gedcomx::Fact{};  // keeps id, value, conclusion 
        fact.type = isCustomType ? relFactTypeCustomBuffer_ : kRelFactTypes[relSelectedFactTypeIndex_];
        fact.date = relFactDate_;
        fact.place.original = relFactPlaceBuffer_;
        fact.place.descriptionId = relFactPlaceId_;

        if (relEditingFactIndex_ >= 0) {
            workingRelationship_.facts[relEditingFactIndex_] = fact;
        }
        else {
            fact.id = idgen::generateLocalId("F_");
            workingRelationship_.facts.push_back(fact);
        }
        resetRelFactForm();
    }
    ImGui::EndDisabled();

    if (relEditingFactIndex_ >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel Edit")) resetRelFactForm();
    }
}

void Window::renderRelationshipEditorCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources) {
    renderCitationRefs(CitationEditorOwner::Relationship, workingRelationship_.conclusion.sources, knownSources);
}

void Window::renderRelationshipEditorAttributesTab() {}

void Window::requestNewRelationshipEditor() {
    workingRelationship_ = gedcomx::Relationship{};
    resetRelFactForm();

    for (int i = 0; i < 2; ++i) {
        personSearchBuffer_[i][0] = '\0';
        personSearchResults_[i].clear();
        selectedPersonIndex_[i] = -1;
        pendingSearchQuery_[i].reset();
    }
    relationshipSourceId_.clear();

    editingExistingRelationship_ = false;

    showNewRelationshipModal_ = true;

    resetChildrenTabState();
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
    
    resetRelFactForm();

    showNewRelationshipModal_ = true;

    resetChildrenTabState();
    pendingChildLoad_ = std::make_pair(p1.id, p2.id);
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

        if (personEditorForChild_) renderPersonEditor(knownSources);  // Person Editor loading
        renderCitationEditor(CitationEditorOwner::Relationship, knownSources);
        if (auto picked = renderDateEditor(DateEditorTarget::RelationshipFact)) {
            relFactDate_ = *picked;
            strncpy_s(relFactDateBuffer_, picked->original.c_str(), _TRUNCATE);
        }

        ImGui::Separator();
        bool noPerson1 = selectedPersonIndex_[0] < 0 || selectedPersonIndex_[0] >= (int)personSearchResults_[0].size();
        bool noPerson2 = selectedPersonIndex_[1] < 0 || selectedPersonIndex_[1] >= (int)personSearchResults_[1].size();

        ImGui::BeginDisabled(noPerson1 || noPerson2);
        if (ImGui::Button("Save")) {
            RelationshipSaveResult out;
            out.sourceId = relationshipSourceId_;
            out.isEdit = editingExistingRelationship_;

            const std::string p1 = storage::stripRowKeyPrefix(personSearchResults_[0][selectedPersonIndex_[0]].id, relationshipSourceId_);
            const std::string p2 = storage::stripRowKeyPrefix(personSearchResults_[1][selectedPersonIndex_[1]].id, relationshipSourceId_);

            out.couple = workingRelationship_;
            if (!editingExistingRelationship_)
                out.couple.id = idgen::generateLocalId("R_");
            out.couple.type = "http://gedcomx.org/Couple";
            out.couple.person1Id = p1;
            out.couple.person2Id = p2;

            auto inputs = collectChildInputs(2, out.newPersons);

            auto plan = gedcomx::planChildLinks(p1, p2, inputs, removedChildLinks_,
                [] { return idgen::generateLocalId("R_"); });
            out.childLinks = std::move(plan.save);
            out.deleteLinksIds = std::move(plan.deleteIds);

            relationshipSaveResult_ = std::move(out);
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

std::optional<std::pair<std::string, std::string>> Window::consumePersonSearchQuery(int pickerIndex) {
    if (!pendingSearchQuery_[pickerIndex]) return std::nullopt;
    std::string query = *pendingSearchQuery_[pickerIndex];
    pendingSearchQuery_[pickerIndex].reset();

    // Picker 0 (Person 1) is unrestricted; picker 1 (Person 2) is scoped to
    // whichever source Person 1 belongs to.
    std::string sourceFilter = (pickerIndex >= 1) ? relationshipSourceId_ : "";
    return std::make_pair(query, sourceFilter);
}

void Window::setPersonSearchResults(int pickerIndex, const std::vector<gedcomx::Person>& results) {
    personSearchResults_[pickerIndex] = results;
}

void Window::resetChildrenTabState() {
    stagedChildren_.clear();
    removedChildLinks_.clear();
    pendingChildLoad_.reset();
    personSearchBuffer_[2][0] = '\0';
    personSearchResults_[2].clear();
    pendingSearchQuery_[2].reset();
}

std::optional<std::pair<std::string, std::string>> Window::consumeChildLoadRequest() {
    auto r = pendingChildLoad_;
    pendingChildLoad_.reset();
    return r;
}

void Window::setExistingChildren(const std::vector<ExistingChild>& children) {
    stagedChildren_.clear();
    removedChildLinks_.clear();
    for (const auto& ec : children) {
        StagedChild sc;
        sc.person = ec.child;
        for (int slot = 0; slot < 2; ++slot) {
            sc.link[slot] = ec.link[slot];
            sc.parentage[slot] = ec.link[slot] ? parentageIndexFor(*ec.link[slot]) : kNotAParent;
        }
        stagedChildren_.push_back(std::move(sc));
    }
}

std::optional<Window::RelationshipSaveResult> Window::consumeRelationshipSaveResult() {
    auto r = std::move(relationshipSaveResult_);
    relationshipSaveResult_.reset();
    return r;
}

void Window::requestAddChildren(const gedcomx::Person& parent) {
    resetChildrenTabState();
    soloParent_ = parent;
    relationshipSourceId_ = parent.sourceId;  // children must share the parent's source
    pendingChildLoad_ = std::make_pair(parent.id, std::string());  // second key empty: one parent
    showChildrenEditor_ = true;
}

void Window::renderChildrenEditor(const std::vector<gedcomx::SourceDescription>& knownSources) {
    if (showChildrenEditor_ && !ImGui::IsPopupOpen("Edit Children")) {
        ImGui::OpenPopup("Edit Children");
    }

    ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Edit Children", &showChildrenEditor_)) {
        ImGui::Text("Children of %s", soloParent_.primaryFullText().c_str());
        ImGui::Separator();

        renderChildrenEditorBody(&soloParent_, nullptr);
        if (personEditorForChild_) renderPersonEditor(knownSources);  // "+ New person" renders inside this popup

        ImGui::Separator();
        if (ImGui::Button("Save")) {
            RelationshipSaveResult out;
            out.saveCouple = false;
            out.sourceId = relationshipSourceId_;

            const std::string parentId = storage::stripRowKeyPrefix(soloParent_.id, relationshipSourceId_);
            auto inputs = collectChildInputs(1, out.newPersons);
            auto plan = gedcomx::planChildLinks(parentId, "", inputs, removedChildLinks_,
                [] { return idgen::generateLocalId("R_"); });
            out.childLinks = std::move(plan.save);
            out.deleteLinksIds = std::move(plan.deleteIds);
                
            relationshipSaveResult_ = std::move(out);
            showChildrenEditor_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showChildrenEditor_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}