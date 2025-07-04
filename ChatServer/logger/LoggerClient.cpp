#include "LoggerClient.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <iostream>
#include <filesystem>

const char *SOCKET_PATH = "/tmp/loggerd.sock";

std::string LoggerClient::GetLevelString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string LoggerClient::GetTimestampString(const std::chrono::system_clock::time_point &time)
{
    auto time_t = std::chrono::system_clock::to_time_t(time);
    //  计算出当前时间点距离纪元（epoch）以来的毫秒数，并取模 1000 得到当前秒内的毫秒部分。
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  time.time_since_epoch()) %
              1000;

    std::tm tm_buf;
    localtime_r(&time_t, &tm_buf); // 线程安全

    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string LoggerClient::FormatLogMessage(const LogContext &context, const std::string &message)
{
    std::stringstream ss;
    ss << "[" << GetTimestampString(context.timestamp) << "] "
       << "[" << GetLevelString(context.level) << "] "
       << "[" << std::filesystem::path(context.filename).filename().string() << ":"
       << context.line << ":" << context.function << "] "
       << message;
    return ss.str();
}

void LoggerClient::Send(LogLevel level, const std::string &message,
                        const std::string &filename, int line, const std::string &function)
{
    // 级别日志过滤设置
    // if (level == LogLevel::DEBUG) return;

    LogContext context{
        filename,
        line,
        function,
        level,
        std::chrono::system_clock::now()};

    std::string formattedMessage = FormatLogMessage(context, message);

    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        std::cerr << "创建UNIX域socket失败: " << strerror(errno) << std::endl;
        return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    ssize_t ret = sendto(sock, formattedMessage.c_str(), formattedMessage.size(), 0,
                         (sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        std::cerr << "发送日志失败: " << strerror(errno) << std::endl;
    }
    close(sock);
}
