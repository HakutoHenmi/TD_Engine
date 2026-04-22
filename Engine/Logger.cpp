#include "Logger.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>

namespace Engine {

bool Logger::isInitialized_ = false;
static std::ofstream logFile_;

void Logger::Initialize() {
    if (isInitialized_) return;

    std::filesystem::path logDir = "logs/app";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directories(logDir);
    }

    logFile_.open("logs/app/app.log", std::ios::out | std::ios::trunc);
    if (logFile_.is_open()) {
        isInitialized_ = true;
        Log("Logger initialized.");
    }
}

void Logger::Log(const std::string& message) {
    if (!isInitialized_) return;

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm bt{};
#ifdef _WIN32
    localtime_s(&bt, &in_time_t);
#else
    localtime_r(&in_time_t, &bt);
#endif

    logFile_ << "[" << std::put_time(&bt, "%Y-%m-%d %H:%M:%S") << "] [INFO] " << message << std::endl;
}

void Logger::LogError(const std::string& message) {
    if (!isInitialized_) return;

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm bt{};
#ifdef _WIN32
    localtime_s(&bt, &in_time_t);
#else
    localtime_r(&in_time_t, &bt);
#endif

    logFile_ << "[" << std::put_time(&bt, "%Y-%m-%d %H:%M:%S") << "] [ERROR] " << message << std::endl;
}

void Logger::Shutdown() {
    if (isInitialized_) {
        Log("Logger shutting down.");
        logFile_.close();
        isInitialized_ = false;
    }
}

} // namespace Engine
