#include "window.h"

void Window::renderTitleBarAndMenu(const std::vector<gedcomx::SourceDescription>& knownSources) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, kTitleBarHeight));

    ImGuiWindowFlags titleBarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar;

    // One color across the whole 32px strip, with the menu text centered in it
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pad = (kTitleBarHeight - ImGui::GetFontSize()) * 0.5f;  // mennu bar height = font + 2 * pad = kTitleBarHeight
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, pad));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.Colors[ImGuiCol_MenuBarBg]);

    ImGui::Begin("TitleBar", nullptr, titleBarFlags);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) {
                requestNewProjectDialog();
            }
            if (ImGui::MenuItem("Open Project...")) {
                requestOpenProjectDialog();
            }
            if (ImGui::MenuItem("Save Project...")) {
                requestSaveProject();
            }
            ImGui::Separator();
            ImGui::BeginDisabled(!projectOpen_);
            if (ImGui::MenuItem("New Source...")) {
                requestNewSourceEditor();
            }
            if (ImGui::MenuItem("Open Source File...")) {
                requestOpenFileDialog();
            }
            if (ImGui::BeginMenu("Edit Source")) {
                for (const auto& source : knownSources) {
                    const std::string label = source.primaryTitle() + "##edit" + source.id;
                    if (ImGui::MenuItem(label.c_str()))
                        requestEditSourceEditor(source);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Export Source...")) {
                requestExportModal();
            }
            if (ImGui::MenuItem("Back Up Project...")) {
                requestBackupDialog();
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                running_ = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::BeginDisabled(!projectOpen_);
            if (ImGui::MenuItem("Go to Person...")) {
                requestGoToPersonModal();
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::MenuItem("Layers");
            if (ImGui::MenuItem("Log")) {
                toggleLogWindow();
            }
            if (ImGui::MenuItem("Settings")) {
                toggleSettingsPanel();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Source Layer")) {
                if (ImGui::MenuItem("All Sources", nullptr, viewMode_ == ViewMode::AllSources)) {
                    viewMode_ = ViewMode::AllSources;
                }
                if (ImGui::MenuItem("Aggregate (Merged)", nullptr, viewMode_ == ViewMode::Aggregate)) {
                    viewMode_ = ViewMode::Aggregate;
                }
                if (!knownSources.empty()) ImGui::Separator();
                for (const auto& source : knownSources) {
                    bool isCurrent = (viewMode_ == ViewMode::SingleSource && viewModeSourceId_ == source.id);
                    std::string label = source.primaryTitle() + "##" + source.id;  // Id collision fix
                    if (ImGui::MenuItem(label.c_str(), nullptr, isCurrent)) {
                        viewMode_ = ViewMode::SingleSource;
                        viewModeSourceId_ = source.id;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        const bool dataMenuOpen = ImGui::BeginMenu("Data");
        menuRightEdge_ = (int)ImGui::GetItemRectMax().x;  // the last menu header: the title strip starts after it
        if (dataMenuOpen) {
            ImGui::BeginDisabled(!projectOpen_);
            if (ImGui::MenuItem("New Person...")) {
                requestNewPersonEditor();
            }
            if (ImGui::MenuItem("New Relationship...")) {
                requestNewRelationshipEditor();
            }
            if (ImGui::MenuItem("New Place...")) {
                requestNewPlaceEditor();
            }
            if (ImGui::MenuItem("Places...")) {
                requestPlaceList();
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        ImGui::Text("  WHP - Genealogy Visualizer");
        ImGui::EndMenuBar();
    }

    renderNewProjectModal();
    renderSwitchConfirmModal();

    ImGui::End(); 

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    renderWindowButtons();
}

void Window::renderWindowButtons() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetForegroundDrawList();  // above every window: a modal popup cannot block or cover these
    const bool maximized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
    const float width = io.DisplaySize.x;
    const ImU32 ink = IM_COL32(230, 230, 230, 255);
    const float s = 5.0f;

    for (int i = 0; i < 3; ++i) {  // 0 minimize, 1 maximize or restore, 2 close
        const ImVec2 lo(width - (3 - i) * kWindowButtonWidth, 0.0f);
        const ImVec2 hi(lo.x + kWindowButtonWidth, kTitleBarHeight);
        const bool hovered = io.MousePos.x >= lo.x && io.MousePos.x < hi.x && io.MousePos.y >= lo.y && io.MousePos.y < hi.y;

        if (hovered) draw->AddRectFilled(lo, hi, i == 2 ? IM_COL32(200, 40, 40, 255) : IM_COL32(255, 255, 255, 40));

        const ImVec2 c((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
        if (i == 0) {
            draw->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), ink, 1.0f);
        }
        else if (i == 1) {
            if (maximized) {  // two overlapping squares mean "restore"
                draw->AddRect(ImVec2(c.x - s + 2.0f, c.y - s), ImVec2(c.x + s, c.y + s - 2.0f), ink);
                draw->AddRect(ImVec2(c.x - s, c.y - s + 2.0f), ImVec2(c.x + s - 2.0f, c.y + s), ink);
            }
            else {
                draw->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), ink);
            }
        }
        else {
            draw->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), ink, 1.0f);
            draw->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), ink, 1.0f);
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            pressedWindowButton_ = i;
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && pressedWindowButton_ == i) {  // act on release, like real buttons
            if (i == 0) {
                SDL_MinimizeWindow(window_);
            }
            else if (i == 1) {
                if (maximized) SDL_RestoreWindow(window_);
                else SDL_MaximizeWindow(window_);
            }
            else {
                running_ = false;
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        pressedWindowButton_ = -1;
}