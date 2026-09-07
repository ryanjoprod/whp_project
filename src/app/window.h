#pragma	once

#include <sdl3/SDL.h>

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
	void renderCanvas();
	void endFrame();

private:
	SDL_Window* window_ = nullptr;
	SDL_Renderer* renderer_ = nullptr;
	bool running_ = true;

	bool draggingWindow_ = false;
	float dragStartMouseX_ = 0, dragStartMouseY_ = 0;
	int dragStartWindowX_ = 0, dragStartWindowY_ = 0;
};