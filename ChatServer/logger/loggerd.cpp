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
#include <syslog.h>
#include <sys/time.h>


const char *SOCKET_PATH = "/tmp/loggerd.sock";
const char *LOG_DIR = "/root/Mylogs/"; // 使用全局路径

volatile std::sig_atomic_t g_running = 1; // 进程运行标志
// 使用 std::sig_atomic_t，编译器会确保对该变量的读写操作是原子的，不会被中断
// volatile告诉编译器:每次都要从内存读取这个变量的值，不要缓存。
// 当变量可能在程序外部或者异步事件中被修改时，使用volatile可以防止编译器优化导致的错误。

void SignalHandler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
    {
        g_running = 0; // 收到退出信号，设置标志，优雅退出
        syslog(LOG_INFO, "Signal %d received, exiting...", signo);
    }
}

void Daemonize()
{
    if (fork() > 0)
        exit(0);
    // 当前进程成为子进程
    setsid();
    // 创建一个新的会话，成为会话首进程
    if (fork() > 0)
        exit(0);
    // 当前进程成为孙子进程
    // 保证守护进程永远不会成为会话首进程，从而防止它重新获得控制终端（只有会话首进程才有可能重新获得终端）
    umask(0);
    // 用于设置进程创建新文件或目录时的默认权限掩码
    [[maybe_unused]] int res = chdir("/");
    // 切换到根目录 / 是为了让守护进程与具体的文件系统路径解耦，防止资源被占用
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    // 关闭标准输入输出错误流，防止守护进程占用终端
}

// 获取当天的日志文件名
// 格式为 YYYY-MM-DD.log
std::string GetLogFileName()
{
    time_t now = time(nullptr);
    char buf[32];
    // “string format time”，可以把时间time_t转换成指定格式的字符串
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
        // 如果今天的日志文件还没有打开，则创建它
        std::filesystem::create_directory(LOG_DIR);
        std::string logPath = std::string(LOG_DIR) + today;
        logs[today].open(logPath, std::ios::app);
        // 自动插入一个新的 key（today）到 logs 中
        // ofstream打开文件时，如果文件不存在则会创建它,存在则会打开
        // 这里使用 std::ios::app 模式打开文件，表示以追加模式
        // 打开文件流，参数 std::ios::app 表示以追加模式打开文件（写入的数据会追加到文件末尾，而不是覆盖）
    }
    return logs[today];
}

int main()
{
    // 打开 syslog，指定标识和选项，守护进程一般用 LOG_DAEMON
    // LOG_PID：在日志消息中包含进程ID。LOG_CONS：如果无法写入日志，尝试写到系统控制台。
    openlog("loggerd", LOG_PID | LOG_CONS, LOG_DAEMON);
    // void openlog(const char *ident, int option, int facility);
    // ident：日志标识符，通常是程序名；option：日志选项；facility：日志设施，指定日志的类别。

    // 最开始就守护进程化
    Daemonize();
    // 注册信号处理，优雅退出
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    syslog(LOG_INFO, "守护进程启动，pid=%d", getpid());

    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (server_fd < 0)
    {
        syslog(LOG_ERR, "创建 socket 失败: %s", strerror(errno));
        closelog();
        return 1;
    }

    struct timeval timeout;
    timeout.tv_sec = 1; // recv每次最多阻塞 1 秒就会检查循环条件
    timeout.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    // 绑定前先删除旧的socket文件，防止bind失败
    std::filesystem::remove(SOCKET_PATH);

    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        syslog(LOG_ERR, "绑定 socket 失败: %s", strerror(errno));
        close(server_fd);
        closelog();
        return 1;
    }
    // bind这一步会在文件系统的 tmp 目录下自动创建一个名为 loggerd.sock 的 socket 文件

    // 将 loggerd.sock 这个 Unix 域套接字文件的权限设置为 0666，即所有用户都可以读写
    chmod(SOCKET_PATH, 0666);

    std::map<std::string, std::ofstream> logFiles;
    char buffer[4096];

    while (g_running)
    {
        ssize_t n = recv(server_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0)
        {
            buffer[n] = '\0';
            std::string msg(buffer);

            std::ofstream &log = GetLogStream(logFiles);
            if (log.is_open()) // 判断文件流对象当前是否已经成功打开了一个文件
            {
                log << msg << std::endl;
                log.flush();
            }
            else
            {
                syslog(LOG_ERR, "打开日志文件失败，无法写入消息");
            }
        }
        else if (n == -1)
        {
            if (errno == EINTR)
            {
                if (!g_running)
                    break; // 信号打断 + 退出标志已设定
                continue;
            }
            syslog(LOG_ERR, "接收数据失败: %s", strerror(errno));
        }
    }

    syslog(LOG_INFO, "收到退出信号，守护进程准备关闭");

    close(server_fd);
    std::filesystem::remove(SOCKET_PATH);
    for (auto &pair : logFiles)
        pair.second.close();

    closelog();
    return 0;
}
