#include <iostream>
#include <fstream>

#include "app.h"
#include "app_settings.h"
#include "logger.h"

int main() {
	Logger::init("whp_program.log");

	AppSettings appSettings;
	std::ifstream settingsFile("app_settings.json");
	if (settingsFile) {
		std::string jsonText((std::istreambuf_iterator<char>(settingsFile)), std::istreambuf_iterator<char>());
		appSettings = deserializeAppSettings(jsonText);
	}

	try {
		App app(appSettings);
		app.run();
	}
	catch (const std::exception& e) {
		Logger::log(LogLevel::Error, std::string("Fatal error: ") + e.what());
		return 1;
	}

	return 0;
}