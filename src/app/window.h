#pragma	once

#include <vector>
#include <unordered_map>
#include <string>

#include <sdl3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "gedcomx/person.h"
#include "gedcomx/relationship.h"

class Window {
public:
	Window();
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	bool isRunning() const;
	void processEvents();
	void beginFrame();
	void renderTitleBarAndMenu();
	void renderCanvas(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships);
	void endFrame();

private:
	SDL_Window* window_ = nullptr;
	SDL_Renderer* renderer_ = nullptr;
	bool running_ = true;

	// Window movement and position
	bool draggingWindow_ = false;
	float dragStartMouseX_ = 0, dragStartMouseY_ = 0;
	int dragStartWindowX_ = 0, dragStartWindowY_ = 0;

	// Layout state
	std::unordered_map<std::string, ImVec2> nodePositions_;
	std::unordered_map<std::string, ImVec2> nodeVelocities_;

	// Layout methods
	void updateLayout(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships, ImVec2 canvasOrigin, ImVec2 canvasSize);
};