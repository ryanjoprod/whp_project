#include <stdexcept>

#include "window.h"

Window::Window() {
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
	}

	window_ = SDL_CreateWindow("WHP - Geneaology Visualizer", 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
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

void Window::endFrame() {
	ImGui::Render();
	SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
	SDL_RenderClear(renderer_);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
	SDL_RenderPresent(renderer_);
}