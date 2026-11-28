#include <SDL3/SDL_main.h>

#include <fstream>
#include <iterator>
#include <string>

#include "app.h"
#include "app_paths.h"
#include "logger.h"
#include "window_state.h"

int main(int argc, char* argv[]) {
	const auto dataFolder = userDataFolder();
	Logger::init(dataFolder / "whp_program.log");
	Logger::log(LogLevel::Info, std::string("WHP Program ") + WHP_VERSION);

	WindowState windowState;
	std::ifstream stateFile(dataFolder / "window_state.json");

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