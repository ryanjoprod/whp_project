#pragma once

#include <filesystem>

// Per-user folder for app-wide files (window state). SDL created it if needed.
// Falls back to the current folder if the OS gives no answer.
std::filesystem::path userDataFolder();