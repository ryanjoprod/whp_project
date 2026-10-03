#include "window.h"

void Window::renderTitleBarAndMenu(const std::vector<gedcomx::SourceDescription>& knownSources) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, kTitleBarHeight));

    ImGuiWindowFlags titleBarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar;

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
            if (ImGui::MenuItem("Open Source File...")) {
                requestOpenFileDialog();
            }
            if (ImGui::MenuItem("Export Source...")) {
                requestExportModal();
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                running_ = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
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
        if (ImGui::BeginMenu("Data")) {
            ImGui::BeginDisabled(!projectOpen_);
            if (ImGui::MenuItem("New Person...")) {
                requestNewPersonEditor();
            }
            if (ImGui::MenuItem("New Relationship...")) {
                requestNewRelationshipModal();
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        ImGui::Text("  WHP - Genealogy Visualizer");

        ImGui::SameLine(ImGui::GetWindowWidth() - 80);
        if (ImGui::Button("[ ]", ImVec2(30, 0))) {
            bool isMaximized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
            if (isMaximized) SDL_RestoreWindow(window_);
            else SDL_MaximizeWindow(window_);
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 40);
        if (ImGui::Button("X", ImVec2(30, 0))) {
            running_ = false;
        }

        ImGui::EndMenuBar();
    }

    renderNewProjectModal();
    renderSwitchConfirmModal();

    bool titleBarHovered = ImGui::IsWindowHovered();
    bool titleBarClickable = titleBarHovered && !ImGui::IsAnyItemHovered();

    if (titleBarClickable && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        bool isMaximized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
        if (isMaximized) SDL_RestoreWindow(window_);
        else SDL_MaximizeWindow(window_);
    }
    else if (titleBarClickable && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        bool isMaximized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
        if (!isMaximized) {
            draggingWindow_ = true;
            SDL_GetGlobalMouseState(&dragStartMouseX_, &dragStartMouseY_);
            SDL_GetWindowPosition(window_, &dragStartWindowX_, &dragStartWindowY_);
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggingWindow_ = false;
    }

    if (draggingWindow_) {
        float currentMouseX, currentMouseY;
        SDL_GetGlobalMouseState(&currentMouseX, &currentMouseY);
        int dx = (int)(currentMouseX - dragStartMouseX_);
        int dy = (int)(currentMouseY - dragStartMouseY_);
        SDL_SetWindowPosition(window_, dragStartWindowX_ + dx, dragStartWindowY_ + dy);
    }

    ImGui::End();
}