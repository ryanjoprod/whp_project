#include <random>
#include <algorithm>
#include <cfloat>
#include <unordered_set>
#include <cmath>

#include "window.h"
#include "id_generator.h"
#include "storage/database.h"

namespace {
    // Squared disatnce from point p to segment ab (all in screen space)
    float distSqToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
        float abx = b.x - a.x, aby = b.y - a.y;
        float lenSq = abx * abx + aby * aby;
        float t = lenSq > 0.0f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);  // keep the closest point on the segment, not the infinite line
        float cx = a.x + t * abx - p.x;
        float cy = a.y + t * aby - p.y;
        return cx * cx + cy * cy;
    }
}

void Window::updateLayout(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships) {
    static std::mt19937 rng(12345);
    std::uniform_real_distribution<float> jitter(-50.0f, 50.0f);

    ImVec2 worldCenter(0.0f, 0.0f);
    for (const auto& person : people) {
        if (nodePositions_.find(person.id) == nodePositions_.end()) {
            nodePositions_[person.id] = ImVec2(worldCenter.x + jitter(rng), worldCenter.y + jitter(rng));
            nodeVelocities_[person.id] = ImVec2(0, 0);
        }
    }

    const float repulsionStrength = settings_.repulsionStrength;
    const float springStrength = settings_.springStrength;
    const float restLength = settings_.restLength;
    const float damping = settings_.damping;
    const float centeringStrength = settings_.centeringStrength;

    std::unordered_map<std::string, ImVec2> forces;
    for (const auto& person : people) forces[person.id] = ImVec2(0, 0);

    for (size_t i = 0; i < people.size(); ++i) {
        for (size_t j = i + 1; j < people.size(); ++j) {
            ImVec2& posA = nodePositions_[people[i].id];
            ImVec2& posB = nodePositions_[people[j].id];

            float dx = posA.x - posB.x;
            float dy = posA.y - posB.y;
            float distSq = std::max(dx * dx + dy * dy, 1.0f);
            float dist = std::sqrt(distSq);

            float force = repulsionStrength / distSq;
            float fx = (dx / dist) * force;
            float fy = (dy / dist) * force;

            forces[people[i].id].x += fx;
            forces[people[i].id].y += fy;
            forces[people[j].id].x -= fx;
            forces[people[j].id].y -= fy;
        }
    }

    for (const auto& rel : relationships) {
        if (!nodePositions_.count(rel.person1Id) || !nodePositions_.count(rel.person2Id)) continue;

        ImVec2& posA = nodePositions_[rel.person1Id];
        ImVec2& posB = nodePositions_[rel.person2Id];

        float dx = posB.x - posA.x;
        float dy = posB.y - posA.y;
        float dist = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);

        float force = springStrength * (dist - restLength);
        float fx = (dx / dist) * force;
        float fy = (dy / dist) * force;

        forces[rel.person1Id].x += fx;
        forces[rel.person1Id].y += fy;
        forces[rel.person2Id].x -= fx;
        forces[rel.person2Id].y -= fy;
    }

    for (const auto& person : people) {
        ImVec2& pos = nodePositions_[person.id];
        forces[person.id].x += (worldCenter.x - pos.x) * centeringStrength;
        forces[person.id].y += (worldCenter.y - pos.y) * centeringStrength;
    }

    for (const auto& person : people) {
        if (person.id == draggedNodeId_) continue;

        ImVec2& vel = nodeVelocities_[person.id];
        ImVec2& pos = nodePositions_[person.id];
        ImVec2& f = forces[person.id];

        vel.x = (vel.x + f.x) * damping;
        vel.y = (vel.y + f.y) * damping;

        const float maxSpeed = 500.0f;
        float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        if (speed > maxSpeed) {
            vel.x = (vel.x / speed) * maxSpeed;
            vel.y = (vel.y / speed) * maxSpeed;
        }

        pos.x += vel.x * 0.016f;
        pos.y += vel.y * 0.016f;
    }
}

void Window::renderCanvas(
    const std::vector<gedcomx::Person>& people,
    const std::vector<gedcomx::Relationship>& relationships,
    const std::vector<gedcomx::SourceDescription>& knownSources,
    const std::unordered_map<std::string, std::vector<std::string>>& aggregateSourceIds)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + kTitleBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - kTitleBarHeight));

    ImGuiWindowFlags canvasFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Canvas", nullptr, canvasFlags);

    if (people.empty()) {
        ImVec2 center(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f,
            ImGui::GetWindowPos().y + ImGui::GetWindowSize().y * 0.5f);
        const char* message = "No data loaded. Use File -> Open Source File... to get started.";
        ImVec2 textSize = ImGui::CalcTextSize(message);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
            IM_COL32(150, 150, 150, 255), message);
    }

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    const float clickMoveTolerance = 4.0f;

    if (ImGui::IsWindowHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            camera_.pan(delta);
        }

        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float zoomFactor = (wheel > 0) ? 1.1f : (1.0f / 1.1f);
            camera_.zoomAt(mousePos, zoomFactor);
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasOrigin = ImGui::GetWindowPos();
    ImVec2 canvasSize = ImGui::GetWindowSize();
    camera_.setViewport(canvasOrigin, canvasSize);

    updateLayout(people, relationships);

    std::unordered_set<std::string> currentIds;
    for (const auto& person : people) currentIds.insert(person.id);

    for (auto it = nodePositions_.begin(); it != nodePositions_.end(); ) {
        if (!currentIds.count(it->first)) {
            nodeVelocities_.erase(it->first);
            it = nodePositions_.erase(it);
        }
        else {
            ++it;
        }
    }

    if (needsRefit_ && !people.empty()) {
        ImVec2 minB(FLT_MAX, FLT_MAX), maxB(-FLT_MAX, -FLT_MAX);
        for (const auto& person : people) {
            ImVec2 pos = nodePositions_.count(person.id) ? nodePositions_[person.id] : ImVec2(0, 0);
            minB.x = std::min(minB.x, pos.x);
            minB.y = std::min(minB.y, pos.y);
            maxB.x = std::max(maxB.x, pos.x);
            maxB.y = std::max(maxB.y, pos.y);
        }
        camera_.refitToBounds(minB, maxB);
        needsRefit_ = false;
    }

    const float nodeRadius = 20.0f;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && draggedNodeId_.empty() && ImGui::IsWindowHovered()) {
        for (auto it = people.rbegin(); it != people.rend(); ++it) {
            ImVec2 screenPos = camera_.worldToScreen(nodePositions_[it->id]);
            float dx = mousePos.x - screenPos.x;
            float dy = mousePos.y - screenPos.y;
            float hitRadius = nodeRadius * camera_.zoom();
            if (dx * dx + dy * dy <= hitRadius * hitRadius) {
                draggedNodeId_ = it->id;
                dragStartMousePos_ = mousePos;
                break;
            }
        }
    }

    if (!draggedNodeId_.empty()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            nodePositions_[draggedNodeId_] = camera_.screenToWorld(mousePos);
            nodeVelocities_[draggedNodeId_] = ImVec2(0, 0);
        }
        else {
            float dx = mousePos.x - dragStartMousePos_.x;
            float dy = mousePos.y - dragStartMousePos_.y;
            if (dx * dx + dy * dy <= clickMoveTolerance * clickMoveTolerance) {
                selectedPersonId_ = draggedNodeId_;
            }
            draggedNodeId_.clear();
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsWindowHovered()) {
        rightClickStartPos_ = mousePos;
        rightClickArmed_ = true;
    }

    if (rightClickArmed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        rightClickArmed_ = false;
        float mdx = mousePos.x - rightClickStartPos_.x;
        float mdy = mousePos.y - rightClickStartPos_.y;
        bool wasClick = mdx * mdx + mdy * mdy <= clickMoveTolerance * clickMoveTolerance;

        if (wasClick && ImGui::IsWindowHovered()) {
            // Nodes win over lines
            const gedcomx::Person* hitPerson = nullptr;
            for (auto pit = people.rbegin(); pit != people.rend(); ++pit) {
                ImVec2 sp = camera_.worldToScreen(nodePositions_[pit->id]);
                float dx = mousePos.x - sp.x, dy = mousePos.y - sp.y;
                float r = nodeRadius * camera_.zoom();
                if (dx * dx + dy * dy <= r * r) {
                    hitPerson = &*pit;
                    break;
                }
            }

            if (hitPerson) {
                contextPerson_ = *hitPerson;
                selectedPlaceId_ = hitPerson->id;  // like a left click: the Details panel follows
                ImGui::OpenPopup("PersonContext");
            }
            else {
                const float lineHitRadius = 6.0f;
                float bestSq = lineHitRadius * lineHitRadius;
                const gedcomx::Relationship* best = nullptr;
                for (const auto& rel : relationships) {
                    if (rel.type != "http://gedcomx.org/Couple") continue;  // only couples are editable
                    auto it1 = nodePositions_.find(rel.person1Id);
                    auto it2 = nodePositions_.find(rel.person2Id);
                    if (it1 == nodePositions_.end() || it2 == nodePositions_.end()) continue;
                    float d = distSqToSegment(mousePos, camera_.worldToScreen(it1->second), camera_.worldToScreen(it2->second));
                    if (d <= bestSq) {
                        bestSq = d;
                        best = &rel;
                    }
                }
                if (best) {
                    contextRelationship_ = *best;
                    ImGui::OpenPopup("RelationshipContext");
                }
                else {
                    ImGui::OpenPopup("CanvasContext");
                }
            }
        }
    }

    const ImU32 coupleColor = IM_COL32(150, 150, 150, 255);  // gray
    const ImU32 parentChildColor = IM_COL32(214, 164, 84, 255);  // muted amber

    for (const auto& rel : relationships) {
        auto it1 = nodePositions_.find(rel.person1Id);
        auto it2 = nodePositions_.find(rel.person2Id);
        if (it1 == nodePositions_.end() || it2 == nodePositions_.end()) continue;

        const ImVec2 a = camera_.worldToScreen(it1->second);  // person1: the parent
        const ImVec2 b = camera_.worldToScreen(it2->second);  // person2: the child

        if (rel.type != gedcomx::kParentChildType) {
            drawList->AddLine(a, b, coupleColor, 2.0f);
            continue;
        }

        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        const float r = nodeRadius * camera_.zoom();  // child's on-screen radius
        const float arrowLen = std::clamp(r * 0.6f, 6.0f, 14.0f);

        if (len <= r + arrowLen) {
            drawList->AddLine(a, b, parentChildColor, 2.0f);
            continue;
        }

        const ImVec2 dir(dx / len, dy / len);
        const ImVec2 perp(-dir.y, dir.x);
        const ImVec2 tip(b.x - dir.x * r, b.y - dir.y * r);  // touches the child's edge
        const ImVec2 base(tip.x - dir.x * arrowLen, tip.y - dir.y * arrowLen);
        const float half = arrowLen * 0.45f;

        drawList->AddLine(a, base, parentChildColor, 2.0f);
        drawList->AddTriangleFilled(tip,
            ImVec2(base.x + perp.x * half, base.y + perp.y * half),
            ImVec2(base.x - perp.x * half, base.y - perp.y * half),
            parentChildColor);
    }

    for (const auto& person : people) {
        ImVec2 worldPos = nodePositions_[person.id];
        ImVec2 screenPos = camera_.worldToScreen(worldPos);
        float screenRadius = nodeRadius * camera_.zoom();

        drawList->AddCircleFilled(screenPos, screenRadius, IM_COL32(70, 130, 200, 255));
        drawList->AddCircle(screenPos, screenRadius, IM_COL32(255, 255, 255, 255), 0, 2.0f);

        if (camera_.zoom() > settings_.lodZoomThreshold) {
            std::string label = person.primaryFullText();
            ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y - textSize.y * 0.5);
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), label.c_str());
        }
    }

    renderContextMenus(people);

    ImGui::End();

    renderDetailsPanel(people, relationships, knownSources, aggregateSourceIds);
}

void Window::renderDetailsPanel(
    const std::vector<gedcomx::Person>& people,
    const std::vector<gedcomx::Relationship>& relationships,
    const std::vector<gedcomx::SourceDescription>& knownSources,
    const std::unordered_map<std::string, std::vector<std::string>>& aggregateSourceIds)
{
    if (selectedPersonId_.empty()) return;

    auto it = std::find_if(people.begin(), people.end(),
        [&](const gedcomx::Person& p) { return p.id == selectedPersonId_; });
    if (it == people.end()) return;

    const gedcomx::Person& person = *it;

    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    bool open = true;
    fitNextWindow("Person Details");
    if (ImGui::Begin("Person Details", &open)) {
        ImGui::Text("%s", person.primaryFullText().c_str());
        ImGui::Separator();

        auto multiSrcIt = aggregateSourceIds.find(person.id);
        if (multiSrcIt != aggregateSourceIds.end() && multiSrcIt->second.size() > 1) {
            std::string joined;
            for (size_t i = 0; i < multiSrcIt->second.size(); ++i) {
                auto srcIt = std::find_if(knownSources.begin(), knownSources.end(),
                    [&](const gedcomx::SourceDescription& s) {
                        return s.id == multiSrcIt->second[i];
                    });
                joined += srcIt != knownSources.end() ? srcIt->primaryTitle() : multiSrcIt->second[i];
                if (i + 1 < multiSrcIt->second.size()) joined += ", ";
            }
            ImGui::TextDisabled("Sources: %s", joined.c_str());
        }
        else if (!person.sourceId.empty()) {
            auto srcIt = std::find_if(knownSources.begin(), knownSources.end(),
                [&](const gedcomx::SourceDescription& s) {
                    return s.id == person.sourceId;
                });
            std::string sourceLabel = srcIt != knownSources.end() ? srcIt->primaryTitle() : person.sourceId;
            ImGui::TextDisabled("Source: %s", sourceLabel.c_str());
        }

        if (!person.facts.empty()) {
            ImGui::Spacing();
            ImGui::Text("Facts:");
            for (const auto& fact : person.facts) {
                ImGui::BulletText("%s: %s %s",
                    gedcomx::formatTypeUri(fact.type).c_str(),
                    fact.date.original.c_str(),
                    fact.place.original.c_str());
            }
        }

        bool shownHeader = false;
        for (int r = 0; r < (int)relationships.size(); ++r) {
            const auto& rel = relationships[r];
            if (rel.type != "http://gedcomx.org/Couple") continue;
            bool isP1 = rel.person1Id == person.id;
            if (!isP1 && rel.person2Id != person.id) continue;

            const std::string& partnerId = isP1 ? rel.person2Id : rel.person1Id;
            auto pit = std::find_if(people.begin(), people.end(),
                [&](const gedcomx::Person& p) { return p.id == partnerId; });

            if (!shownHeader) { 
                ImGui::Spacing();
                ImGui::Text("Relationships:");
                shownHeader = true;
            }
            ImGui::PushID(r);
            ImGui::BulletText("%s", pit != people.end() ? pit->primaryFullText().c_str() : partnerId.c_str());
            for (const auto& f : rel.facts) {
                ImGui::BulletText("  %s: %s %s", gedcomx::formatTypeUri(f.type).c_str(), f.date.original.c_str(), f.place.original.c_str());
            }
            const bool aggregate = (viewMode_ == ViewMode::Aggregate);
            ImGui::BeginDisabled(pit == people.end() || aggregate);
            if (ImGui::SmallButton("Edit...") && pit != people.end() && !aggregate) {
                requestEditRelationshipEditor(rel, isP1 ? person : *pit, isP1 ? *pit : person);
            }
            ImGui::EndDisabled();
            if (aggregate && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Switch to All Sources or a single source to edit");
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(aggregate);
            if (ImGui::SmallButton("Delete...") && !aggregate) {
                const std::string partnerName = pit != people.end() ? pit->primaryFullText() : partnerId;
                requestDelete(DeleteRequest::Kind::Relationship, rel.sourceId + ":" + rel.id,
                    person.primaryFullText() + " and " + partnerName);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        auto nameOf = [&](const std::string& id) {
            auto it = std::find_if(people.begin(), people.end(), [&](const gedcomx::Person& p) { return p.id == id; });
            return it != people.end() ? it->primaryFullText() : std::string("(not in view)");
        };

        auto parentageText = [](const gedcomx::Relationship& rel) {
            std::string t = gedcomx::parentageTypeOf(rel);
            return t.empty() ? std::string() : " - " + gedcomx::formatTypeUri(t);
        };

        bool shownParents = false;
        for (const auto& rel : relationships) {
            if (rel.type != gedcomx::kParentChildType || rel.person2Id != person.id) continue;
            if (!shownParents) {
                ImGui::Spacing();
                ImGui::Text("Parents:");
                shownParents = true;
            }
            ImGui::BulletText("%s%s", nameOf(rel.person1Id).c_str(), parentageText(rel).c_str());
        }

        bool shownChildren = false;
        for (const auto& rel : relationships) {
            if (rel.type != gedcomx::kParentChildType || rel.person1Id != person.id) continue;
            if (!shownChildren) {
                ImGui::Spacing();
                ImGui::Text("Children:");
                shownChildren = true;
            }
            ImGui::BulletText("%s%s", nameOf(rel.person2Id).c_str(), parentageText(rel).c_str());
        }

        ImGui::Spacing();
        const bool aggregateKids = (viewMode_ == ViewMode::Aggregate);
        ImGui::BeginDisabled(aggregateKids);
        if (ImGui::Button("Add / Edit Children...")) requestAddChildren(person);
        ImGui::EndDisabled();
        if (aggregateKids && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Switch to All Sources or a single source to edit");

        ImGui::Spacing();
        if (ImGui::Button("Edit Person...")) {
            requestEditPersonEditor(person);
        }

        if (!person.conclusion.confidence.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Confidence: %s", gedcomx::formatTypeUri(person.conclusion.confidence).c_str());
        }

        if (!person.conclusion.notes.empty()) {
            ImGui::Spacing();
            ImGui::Text("Notes: ");
            for (const auto& note : person.conclusion.notes) {
                ImGui::BulletText("%s", note.text.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Explore from here")) {
            reRootRequestId_ = person.id;
        }
        ImGui::SameLine();
        if (ImGui::Button("Link to another person...")) {
            requestLinkPersonModal(person.id);
        }
        ImGui::SameLine();
        const bool aggregateMode = (viewMode_ == ViewMode::Aggregate);
        ImGui::BeginDisabled(aggregateMode);
        if (ImGui::Button("Delete Person...")) {
            requestDelete(DeleteRequest::Kind::Person, person.id, person.primaryFullText());
        }
        ImGui::EndDisabled();
        if (aggregateMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Switch to All Sources or a single source to delete");
        }
    }
    ImGui::End();

    if (!open) selectedPersonId_.clear();
}

std::string Window::consumeReRootRequest() {
    std::string result = reRootRequestId_;
    reRootRequestId_.clear();
    return result;
}

bool Window::editMenuItem(const char* label, bool enabled, const char* verb) {
    const bool aggregate = (viewMode_ == ViewMode::Aggregate);
    ImGui::BeginDisabled(aggregate || !enabled);
    const bool clicked = ImGui::MenuItem(label);
    ImGui::EndDisabled();
    if (aggregate && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Switch to All Sources or a single source to %s", verb);
    return clicked && enabled && !aggregate;
}

void Window::renderContextMenus(const std::vector<gedcomx::Person>& people) {
    if (ImGui::BeginPopup("PersonContext")) {
        if (contextPerson_) {
            const gedcomx::Person& person = *contextPerson_;

            ImGui::TextDisabled("%s", person.primaryFullText().c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Edit Person...")) requestEditPersonEditor(person);
            if (editMenuItem("Add / Edit Children...")) requestAddChildren(person);
            if (editMenuItem("Create Relationship...")) requestNewRelationshipEditor(&person);

            ImGui::Separator();
            if (ImGui::MenuItem("Explore from here")) reRootRequestId_ = person.id;
            if (ImGui::MenuItem("Link to another person..."))requestLinkPersonModal(person.id);

            ImGui::Separator();
            if (editMenuItem("Delete Person...", true, "delete"))
                requestDelete(DeleteRequest::Kind::Person, person.id, person.primaryFullText());
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("RelationshipContext")) {
        if (contextRelationship_) {
            auto findPerson = [&](const std::string& id) {
                return std::find_if(people.begin(), people.end(),
                    [&](const gedcomx::Person& p) {
                        return p.id == id;
                });
            };
            auto p1 = findPerson(contextRelationship_->person1Id);
            auto p2 = findPerson(contextRelationship_->person2Id);
            bool both = p1 != people.end() && p2 != people.end();  // both ends must be in view

            if (editMenuItem("Edit Relationship...", both))
                requestEditRelationshipEditor(*contextRelationship_, *p1, *p2);

            ImGui::Separator();
            if (editMenuItem("Delete Relationship...", both, "delete"))
                requestDelete(DeleteRequest::Kind::Relationship,
                    contextRelationship_->sourceId + ":" + contextRelationship_->id,
                    p1->primaryFullText() + " and " + p2->primaryFullText());
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("CanvasContext")) {
        ImGui::BeginDisabled(!projectOpen_);
        if (ImGui::MenuItem("New Person...")) requestNewPersonEditor();
        if (ImGui::MenuItem("Go to Person...")) requestGoToPersonModal();
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}