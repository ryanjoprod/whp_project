#include <stdexcept>
#include <algorithm>

#include "imgui_internal.h"  // FindWindowByName: lets us read where a popup currently is

#include "window.h"
#include "logger.h"

Window::Window() {
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
	}

	window_ = SDL_CreateWindow("WHP - Geneaology Visualizer", 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
	if (!window_) {
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
	}

	SDL_SetWindowMinimumSize(window_, kMinWindowWidth, kMinWindowHeight);
	if (!SDL_SetWindowHitTest(window_, &Window::hitTestCallback, this)) {
		Logger::log(LogLevel::Warning, std::string("Could not set window hit test: ") + SDL_GetError());
	}
	Logger::log(LogLevel::Info, "SDL version " + std::to_string(SDL_GetVersion()));  // handy if snapping mimsbehaves

	renderer_ = SDL_CreateRenderer(window_, nullptr);
	if (!renderer_) {
		SDL_DestroyWindow(window_);
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
	}
	if (!SDL_SetRenderVSync(renderer_, 1)) {
		Logger::log(LogLevel::Warning, std::string("Could not enable vsync: ") + SDL_GetError());
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
	ImGui_ImplSDLRenderer3_Init(renderer_);
}

Window::~Window() {
	if (liveRedrawInstalled_) SDL_RemoveEventWatch(&Window::eventWatch, this);
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer_);
	SDL_DestroyWindow(window_);
	SDL_Quit();
}

SDL_HitTestResult SDLCALL Window::hitTestCallback(SDL_Window* window, const SDL_Point* area, void* data) {
	const Window* self = static_cast<const Window*>(data);
	int width = 0, height = 0;
	SDL_GetWindowSize(window, &width, &height);

	// The window buttons are ordinary client area: the app draws and clicks them itself
	if (area->y < (int)kTitleBarHeight && area->x >= width - 3 * (int)kWindowButtonWidth)
		return SDL_HITTEST_NORMAL;

	const bool maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
	if (!maximized) {
		const bool left = area->x < kResizeBorder;
		const bool right = area->x >= width - kResizeBorder;
		const bool top = area->y < kResizeBorder;
		const bool bottom = area->y >= height - kResizeBorder;

		if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
		if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
		if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
		if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		if (top) return SDL_HITTEST_RESIZE_TOP;
		if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
		if (left) return SDL_HITTEST_RESIZE_LEFT;
		if (right) return SDL_HITTEST_RESIZE_RIGHT;
	}

	// The strip between the menu and the window buttons is the title bar
	if (area->y < (int)kTitleBarHeight && area->x >= self->menuRightEdge_.load()) return SDL_HITTEST_DRAGGABLE;
	return SDL_HITTEST_NORMAL;
}

// A popup in a small or snapped window gets a scrollbar instead of hanging off the edge
void Window::fitNextWindow(const char* name) {
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float left = viewport->Pos.x + kPopupMargin;
	const float top = viewport->Pos.y + kTitleBarHeight + kPopupMargin;
	const float right = viewport->Pos.x + viewport->WorkSize.x - kPopupMargin;
	const float bottom = viewport->Pos.y + viewport->WorkSize.y - kPopupMargin;

	ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(right - left, bottom - top));

	// ImGui centers a popup only the first time it is ever shown and then keeps its position,
	// so after the window shrinks it can sit past the edge. Pull it back inside every frame.
	if (ImGuiWindow* w = ImGui::FindWindowByName(name)) {
		ImVec2 pos = w->Pos;
		const float maxX = right - std::min(w->Size.x, right - left);
		const float maxY = bottom - std::min(w->Size.y, bottom - top);
		pos.x = std::clamp(pos.x, left, std::max(left, maxX));
		pos.y = std::clamp(pos.y, top, std::max(top, maxY));
		if (pos.x != w->Pos.x || pos.y != w->Pos.y)
			ImGui::SetNextWindowPos(pos);
	}
}

bool Window::beginModal(const char* name, bool* open, ImGuiWindowFlags flags) {
	fitNextWindow(name);
	return ImGui::BeginPopupModal(name, open, flags);
}

void Window::setLiveRedraw(std::function<void()> redraw) {
	liveRedraw_ = std::move(redraw);
	if (!liveRedrawInstalled_) {
		if (SDL_AddEventWatch(&Window::eventWatch, this))
			liveRedrawInstalled_ = true;
		else
			Logger::log(LogLevel::Warning, std::string("Could not add event watch: ") + SDL_GetError());
	}
}

// SDL allows redrawing directly from a watcher on EXPOSED. During an OS resize drag this is what keeps the window alive.
bool SDLCALL Window::eventWatch(void* userdata, SDL_Event* event) {
	Window* self = static_cast<Window*>(userdata);
	if (event->type == SDL_EVENT_WINDOW_EXPOSED && self->allowLiveRedraw_ && self->liveRedraw_)
		self->liveRedraw_();
	return true;
}

bool Window::isRunning() const {
	return running_;
}

void Window::processEvents() {
	allowLiveRedraw_ = true;  // the OS can hold this call for a whole resize drag; the watcher redraws meanwhile
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		ImGui_ImplSDL3_ProcessEvent(&event);
		if (event.type == SDL_EVENT_QUIT) {
			running_ = false;
		}
	}
	allowLiveRedraw_ = false;
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
	// SDL_RenderPresent(renderer_);
	if (!SDL_RenderPresent(renderer_)) Logger::log(LogLevel::Error, std::string("RenderPresent failed: ") + SDL_GetError());
}