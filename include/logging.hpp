#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>

enum LogLevel { DEBUG, INFO, WARNING, ERROR, CRITICAL };

class Logger {
public:
  Logger(const std::string &filename) {
    const std::string logDir = "logging";
    std::filesystem::create_directory(logDir);
    const std::string filepath = logDir + "/" + filename;

    logFile.open(filepath, std::ios::app);
    if (!logFile.is_open()) {
      std::cerr << "Error opening log file." << std::endl;
    };
  }

  ~Logger() { logFile.close(); }

  void log(LogLevel level, const std::string &message) {
    std::time_t now = time(0);
    std::tm *timeinfo = localtime(&now);
    char timestamp[20];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    std::ostringstream logEntry;
    logEntry << "[" << timestamp << "] " << levelToString(level) << ": "
             << message << std::endl;

    if (logFile.is_open()) {
      logFile << logEntry.str();
      logFile.flush(); // write mem buffer to output
    }
  }

private:
  std::ofstream logFile; // File stream for log file

  std::string levelToString(LogLevel level) {
    switch (level) {
    case DEBUG:
      return "DEBUG";
    case INFO:
      return "INFO";
    case ERROR:
      return "ERROR";
    case CRITICAL:
      return "CRITICAL";
    default:
      return "UNKNOWN";
    };
  }
};
