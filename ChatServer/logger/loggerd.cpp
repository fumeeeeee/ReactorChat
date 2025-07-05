#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <csignal>
#include <unistd.h>
#include <ctime>
#include <map>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

const char *SOCKET_PATH = "/tmp/loggerd.sock";
const char *LOG_DIR = "/root/ChatServerOPEN/logs/";

bool running = true;

void Daemonize()
{
    if (fork() > 0)
        exit(0);
    setsid();
    if (fork() > 0)
        exit(0);
    // 保证守护进程永远不会成为会话首进程，从而防止它重新获得控制终端（只有会话首进程才有可能重新获得终端）
    umask(0);
    // 用于设置进程创建新文件或目录时的默认权限掩码
    chdir("/");
    // 切换到根目录 / 是为了让守护进程与具体的文件系统路径解耦，防止资源被占用
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
}

// 获取当天的日志文件名
// 格式为 YYYY-MM-DD.log
std::string GetLogFileName()
{
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&now));
    return std::string(buf) + ".log";
}

// 获取日志流，如果当天的日志文件不存在则创建它
// 使用 std::map 来缓存日志流，避免频繁打开和关闭文件
std::ofstream &GetLogStream(std::map<std::string, std::ofstream> &logs)
{
    std::string today = GetLogFileName();
    if (logs.count(today) == false)
    {
        try
        {
            std::filesystem::create_directory(LOG_DIR);
            std::string logPath = std::string(LOG_DIR) + today;
            logs[today].open(logPath, std::ios::app);
            // 自动插入一个新的 key（today）到 logs 中，并关联一个新的 std::ofstream 对象。
            if (logs[today].is_open() == false)
            {
                std::cerr << "无法打开日志文件: " << logPath << " 错误: " << strerror(errno) << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "创建日志目录或文件失败: " << e.what() << std::endl;
        }
    }
    return logs[today];
}

void HandleSignal(int)
{
    running = false;
}

int main()
{
    signal(SIGINT, HandleSignal);
    signal(SIGTERM, HandleSignal);

    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "创建socket失败: " << strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {   // 这一步会在文件系统的 tmp 目录下自动创建一个名为 loggerd.sock 的 socket 文件
        std::cerr << "绑定socket失败: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }

    // 将 loggerd.sock 这个 Unix 域套接字文件的权限设置为 0666，即所有用户都可以读写
    chmod(SOCKET_PATH, 0666);

    Daemonize();

    std::map<std::string, std::ofstream> logFiles;
    char buffer[4096];

    while (running)
    {
        // SOCK_DGRAM : 
        // 内核会自动为每个数据报分配缓冲区，并按照到达顺序排队
        // 服务器只需要在循环中不断调用 recv，每次都能取出一个完整的数据包
        ssize_t n = recv(server_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0)
        {
            buffer[n] = '\0';
            std::string msg(buffer);

            std::ofstream &log = GetLogStream(logFiles);
            if (log.is_open())// 判断文件流对象当前是否已经成功打开了一个文件
            {
                log << msg << std::endl;
                log.flush();
            }
        }
        else if (n < 0)
        {
            std::cerr << "接收消息失败: " << strerror(errno) << std::endl;
        }
    }

    close(server_fd);
    try
    {
        std::filesystem::remove(SOCKET_PATH);
    }
    catch (const std::exception &e)
    {
        std::cerr << "删除socket文件失败: " << e.what() << std::endl;
    }
    for (auto &pair : logFiles)
        pair.second.close();
    return 0;
}
