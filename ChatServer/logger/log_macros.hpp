#pragma once
#include "LoggerClient.hpp"
#include <fmt/format.h> // fmt格式化库

// (...)与(__VA_ARGS__)
// (fmt,...)与(fmt,##__VA_ARGS__)
// ## 是宏参数前缀连接符，会在预处理时尝试合并或省略空参数对应符号
// 但是这里使用了format库的 fmt::format 等价于 std::format（C++20），但兼容性更好、效率更高
#define LOG_DEBUG(...) LoggerClient::Send(LogLevel::DEBUG, fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(...) LoggerClient::Send(LogLevel::INFO, fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARN(...) LoggerClient::Send(LogLevel::WARN, fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(...) LoggerClient::Send(LogLevel::ERROR, fmt::format(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__)
