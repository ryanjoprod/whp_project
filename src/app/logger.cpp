#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "logger.h"

void Logger::init(const std::string& logFilePath) {
	std::lock_guard<std::mutex> lock(mutex_);
	logFilePath_ = logFilePath;

	// Confirm the file is actually writable now, rather than discovering
	// it silently later on the first real log() call
	std::ofstream test(logFilePath_, std::ios::app);
	if (!test) {
		logFilePath_.clear();  // disable file writing; in-memory logging still works
		entries_.push_back({ LogLevel::Warning, "Could not open log file for writing: " + logFilePath });
	}
}

void Logger::log(LogLevel level, const std::string& message) {
	std::lock_guard<std::mutex> lock(mutex_);

	entries_.push_back({ level, message });
	if (entries_.size() > kMaxEntries) {
		entries_.erase(entries_.begin());  // bounded history - don't grow forever
	}

	if (!logFilePath_.empty()) {
		std::ofstream out(logFilePath_, std::ios::app);  // append, never overwrite
		if (out) {
			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			out << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " ";
			out << (level == LogLevel::Error ? "[ERROR] " : level == LogLevel::Warning ? "[WARN] " : "[INFO] ");
			out << message << "\n";
		}
	}
}

std::vector<LogEntry> Logger::recentEntries() {
	std::lock_guard<std::mutex> lock(mutex_);

	return entries_;  // copy - safe to use after the lock releases
}