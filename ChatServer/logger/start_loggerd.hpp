#pragma once
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>   // for system
#include <string.h>


inline bool isLoggerdRunning() {
    // 用pgrep查找loggerd进程，返回0表示有进程在运行
    int ret = system("pgrep -x loggerd > /dev/null 2>&1");
    return ret == 0;
}

inline bool isSocketValid() {
    const std::string sockPath = "/tmp/loggerd.sock";
    if (!std::filesystem::exists(sockPath)) {
        return false;
    }
    
    // 尝试连接socket，如果能连接说明socket是有效的
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, sockPath.c_str());
    
    int ret = connect(sock, (sockaddr*)&addr, sizeof(addr));
    close(sock);
    return ret == 0;
}

inline void StartLoggerDaemon() {
    const std::string sockPath = "/tmp/loggerd.sock";

    // 如果loggerd正在运行且socket有效，直接返回
    if (isLoggerdRunning() && isSocketValid()) {
        std::cout << "[日志守护进程] 已在运行。\n";
        return;
    }

    // 如果loggerd在运行但socket无效，杀掉它
    if (isLoggerdRunning()) {
        std::cout << "正在清理旧的loggerd进程..." << std::endl;
        system("pkill -9 loggerd");
        sleep(1);  // 等待进程完全退出
    }

    // 删除旧的socket文件
    if (std::filesystem::exists(sockPath)) {
        std::cout << "正在删除旧的socket文件..." << std::endl;
        std::filesystem::remove(sockPath);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程执行 loggerd，父进程继续
        execl("./build/loggerd", "./build/loggerd", nullptr);
        perror("启动 loggerd 失败");
        exit(1);
    } else if (pid > 0) {
        std::cout << "[日志守护进程] 启动中...\n";
        sleep(1); // 等 socket 创建
    } else {
        perror("fork 启动 loggerd 失败");
    }
}
