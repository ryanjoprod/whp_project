#include <iostream>

#include "window.h"

int main() {
	try {
		Window window;

		while (window.isRunning()) {
			window.processEvents();
			window.beginFrame();
			window.renderTitleBarAndMenu();
			window.renderCanvas();
			window.endFrame();
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Fatal error: " << e.what() << "\n";
		return 1;
	}

	return 0;
}