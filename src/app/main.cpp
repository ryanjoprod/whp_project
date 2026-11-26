#include <fstream>
#include <iterator>
#include <string>

#include "app.h"
#include "app_paths.h"
#include "logger.h"
#include "window_state.h"

int main() {
	Logger::init("whp_program.log");

	WindowState windowState;
	std::ifstream stateFile(userDataFolder() / "window_state.json");

	if (stateFile) {
		std::string jsonText((std::istreambuf_iterator<char>(stateFile)), std::istreambuf_iterator<char>());
		windowState = deserializeWindowState(jsonText);
	}

	try {
		App app(windowState);
		app.run();
	}
	catch (const std::exception& e) {
		Logger::log(LogLevel::Error, std::string("Fatal error: ") + e.what());
		return 1;
	}

	return 0;
}