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
#include <cstdlib>
#include <string.h>
#include <sys/wait.h>

inline bool isLoggerdRunning()
{
    // 用pgrep查找loggerd进程，返回0表示有进程在运行
    // 重定向输出让命令执行时不会在终端或日志中产生任何输出，无论是否找到进程
    int ret = system("pgrep -x loggerd > /dev/null 2>&1");
    // 如果pgrep找到了进程，system()会返回0。所以当ret为0时，表示loggerd正在运行。
    return ret == 0;
}

inline bool isSocketValid(const std::string &sockPath)
{
    if (std::filesystem::exists(sockPath) == false)
    {
        return false;
    }

    // 尝试连接socket，如果能连接说明socket是有效的
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, sockPath.c_str());

    int ret = connect(sock, (sockaddr *)&addr, sizeof(addr));
    close(sock);
    return ret == 0; // 如果连接成功，返回0
}

void StartLoggerDaemon()
{
    const std::string sockPath = "/tmp/loggerd.sock";

    // 如果loggerd正在运行且socket有效，直接返回
    if (isLoggerdRunning() && isSocketValid(sockPath))
    {
        std::cout << "[日志守护进程] 已在运行。\n";
        return;
    }

    // 如果loggerd在运行但socket无效，杀掉它
    if (isLoggerdRunning())
    {
        std::cout << "正在清理旧的loggerd进程..." << std::endl;
        [[maybe_unused]] int res = system("pkill -9 loggerd");
        sleep(1); // 等待进程完全退出,避免后续操作进行时旧进程还未彻底关闭,导致资源冲突或操作失败
    }

    // 删除旧的socket文件
    if (std::filesystem::exists(sockPath))
    {
        std::cout << "正在删除旧的socket文件..." << std::endl;
        std::filesystem::remove(sockPath);
    }

    pid_t pid = fork(); // 返回值在父进程中是子进程的 PID，在子进程中是 0，如果出错则返回 -1
    if (pid == 0)
    {
        // 在子进程中，用 loggerd 这个可执行文件替换当前进程，并传递参数
        // 以当前文件的位置为出发点，执行 loggerd 可执行文件
        execl("../build/loggerd", "loggerd", nullptr);
        perror("启动 loggerd 失败");
        exit(1);
    }
    else if (pid > 0)
    {
        // std::cout << "子进程PID: " << pid << "\n";
        std::cout << "[日志守护进程] 启动中...\n";
        sleep(1); // 等 socket 创建

        // 当fork一个子进程时,一定要进行回收退出的子进程，避免僵尸进程的产生
        int status = 0;
        waitpid(pid, &status, 0);
    }
    else
    {
        perror("fork 启动 loggerd 失败");
    }
}
