#pragma once
#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>

enum class LogLevel 
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

struct LogContext 
{
    std::string filename;
    int line;
    std::string function;
    LogLevel level;
    std::chrono::system_clock::time_point timestamp;
};

class LoggerClient 
{
public:
    static void Send(LogLevel level, const std::string& message, const std::string& filename, int line, const std::string& function);
    static std::string FormatLogMessage(const LogContext& context, const std::string& message);
    static std::string GetLevelString(LogLevel level);
    static std::string GetTimestampString(const std::chrono::system_clock::time_point& time);
};
