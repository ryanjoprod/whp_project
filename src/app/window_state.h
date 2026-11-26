#pragma once

#include <string>
#include <vector>

struct IntRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

struct WindowState {
	bool valid = false;  // false = nothing usable saved, use the defaults
	IntRect rect;  // the window's NORMAL position and size (not the maximized one)
	bool maximized = false;
};

std::string serializeWindowState(const WindowState& state);
WindowState deserializeWindowState(const std::string& jsonText);

// Returns a rectangle that is safe to use: at least minW x minH (as far as the display allows),
// no bigger than the display it lands on, and fully inside it. `displays` are the usable areas of the
// connected monitors, primary FIRST. A rectangle that touches no monitor (its monitor was unplugged)
// is centered on the first display. With no displays the rectangle is returned unchanged.
IntRect fitRectToDisplays(const IntRect& saved, const std::vector<IntRect>& displays, int minW, int minH);