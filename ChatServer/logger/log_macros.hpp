#pragma once
#include "LoggerClient.hpp"
#include <fmt/format.h>

// 获取当前文件名（不包含路径）
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// 定义日志宏
#define LOG_DEBUG(...) LoggerClient::Send(LogLevel::DEBUG, fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(...)  LoggerClient::Send(LogLevel::INFO,  fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARN(...)  LoggerClient::Send(LogLevel::WARN,  fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(...) LoggerClient::Send(LogLevel::ERROR, fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
