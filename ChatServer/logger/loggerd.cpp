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

const char* SOCKET_PATH = "/tmp/loggerd.sock";
const char* LOG_DIR = "/root/ChatServerOPEN/logs/";

bool running = true;

void Daemonize() {
    if (fork() > 0) exit(0);
    setsid();
    if (fork() > 0) exit(0);
    umask(0);
    chdir("/");
    fclose(stdin); fclose(stdout); fclose(stderr);
}

std::string GetLogFileName() {
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&now));
    return std::string(buf) + ".log";
}

std::ofstream& GetLogStream(std::map<std::string, std::ofstream>& logs) {
    std::string today = GetLogFileName();
    if (!logs.count(today)) {
        try {
            std::filesystem::create_directory(LOG_DIR);
            std::string logPath = std::string(LOG_DIR) + today;
            logs[today].open(logPath, std::ios::app);
            if (!logs[today].is_open()) {
                std::cerr << "无法打开日志文件: " << logPath << " 错误: " << strerror(errno) << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "创建日志目录或文件失败: " << e.what() << std::endl;
        }
    }
    return logs[today];
}

void HandleSignal(int) {
    running = false;
}

int main() {
    // 确保删除旧的socket文件
    try {
        if (std::filesystem::exists(SOCKET_PATH)) {
            std::filesystem::remove(SOCKET_PATH);
        }
    } catch (const std::exception& e) {
        std::cerr << "删除旧socket文件失败: " << e.what() << std::endl;
        return 1;
    }

    signal(SIGINT, HandleSignal);
    signal(SIGTERM, HandleSignal);

    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        std::cerr << "创建socket失败: " << strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "绑定socket失败: " << strerror(errno) << std::endl;
        close(server_fd);
        return 1;
    }

    // 设置socket权限
    chmod(SOCKET_PATH, 0666);

    Daemonize();

    std::map<std::string, std::ofstream> logFiles;
    char buffer[4096]; 

    while (running) {
        ssize_t n = recv(server_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            std::string msg(buffer);
            
            std::ofstream& log = GetLogStream(logFiles);
            if (log.is_open()) {
                log << msg << std::endl;
                log.flush();
            }
        } else if (n < 0) {
            std::cerr << "接收消息失败: " << strerror(errno) << std::endl;
        }
    }

    close(server_fd);
    try {
        std::filesystem::remove(SOCKET_PATH);
    } catch (const std::exception& e) {
        std::cerr << "删除socket文件失败: " << e.what() << std::endl;
    }
    for (auto& pair : logFiles) pair.second.close();
    return 0;
}
