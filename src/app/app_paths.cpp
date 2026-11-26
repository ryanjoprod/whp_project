#include <SDL3/SDL.h>

#include <string>

#include "logger.h"
#include "app_paths.h"

std::filesystem::path userDataFolder() {
	char* pref = SDL_GetPrefPath("WHP", "WHP Program");
	if (!pref) {
		Logger::log(LogLevel::Warning, std::string("No user data folder, using the current folder: ") + SDL_GetError());
		return std::filesystem::path(".");
	}
	// SDL gives UTF-8; going through u8string keeps non-ASCII user names working on Windows
	std::filesystem::path folder(std::u8string(reinterpret_cast<const char8_t*>(pref)));
	SDL_free(pref);
	return folder;
}