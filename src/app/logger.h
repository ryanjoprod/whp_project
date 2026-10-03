#pragma once

#include <string>
#include <vector>
#include <mutex>

enum class LogLevel { Info, Warning, Error };

struct LogEntry {
	LogLevel level;
	std::string message;
};

// Simple, dependency-free logging utility. Writes to a log file on disk and
// keeps a bounded in-memory history the UI can display. Thread-safe, since
// SDL's async dialog callbacks may log from a non-main thread.
class Logger {
public:
	static void init(const std::string& logFilePath);
	static void log(LogLevel level, const std::string& message);

	static std::vector<LogEntry> recentEntries();

private:
	static inline std::mutex mutex_;
	static inline std::string logFilePath_;
	static inline std::vector<LogEntry> entries_;
	static constexpr size_t kMaxEntries = 200;
};