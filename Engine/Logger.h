#pragma once
#include <string>

namespace Engine {

class Logger {
public:
    static void Initialize();
    static void Log(const std::string& message);
    static void LogError(const std::string& message);
    static void Shutdown();

private:
    static bool isInitialized_;
};

} // namespace Engine
