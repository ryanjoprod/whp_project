#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <stdexcept>

#include "window.h"

namespace {
	constexpr float kTitleBarHeight = 32.0f;
}

Window::Window() {
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
	}

	window_ = SDL_CreateWindow("WHP - Genealogy Visualizer", 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
	if (!window_) {
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
	}

	renderer_ = SDL_CreateRenderer(window_, nullptr);
	if (!renderer_) {
		SDL_DestroyWindow(window_);
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
	ImGui_ImplSDLRenderer3_Init(renderer_);
}

Window::~Window() {
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer_);
	SDL_DestroyWindow(window_);
	SDL_Quit();
}

bool Window::isRunning() const {
	return running_;
}

void Window::processEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		ImGui_ImplSDL3_ProcessEvent(&event);
		if (event.type == SDL_EVENT_QUIT) {
			running_ = false;
		}
	}
}

void Window::beginFrame() {
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
}

void Window::renderTitleBarAndMenu() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// --- Custom Title bar ---
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, kTitleBarHeight));

	ImGuiWindowFlags titleBarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar;

	ImGui::Begin("TitleBar", nullptr, titleBarFlags);

	// --- Menu bar ---
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			ImGui::MenuItem("Open Source File...");
			ImGui::MenuItem("Open Project...");
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) {
				running_ = false;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Layers");
			ImGui::EndMenu();
		}

		ImGui::Text("  WHP - Geneaology Visualizer");

		// Right-aligned close button
		ImGui::SameLine(ImGui::GetWindowWidth() - 40);
		if (ImGui::Button("X", ImVec2(30, 0))) {
			running_ = false;
		}

		ImGui::EndMenuBar();
	}

	// Drag-to-move: uses absolute screen coordinates so movement doesn't
	// lag behind or feed back on itself as the window's own position changes
	bool titleBarHovered = ImGui::IsWindowHovered();
	if (titleBarHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
		draggingWindow_ = true;
		SDL_GetGlobalMouseState(&dragStartMouseX_, &dragStartMouseY_);
		SDL_GetWindowPosition(window_, &dragStartWindowX_, &dragStartWindowY_);
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

void Window::renderCanvas() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	// --- Canvas placeholder: fills the rest of the window ---
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + kTitleBarHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - kTitleBarHeight));

	ImGuiWindowFlags canvasFlags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::Begin("Canvas", nullptr, canvasFlags);
	ImGui::Text("Node-link diagram will render here");
	ImGui::End();
}

void Window::endFrame() {
	ImGui::Render();
	SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
	SDL_RenderClear(renderer_);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
	SDL_RenderPresent(renderer_);
}