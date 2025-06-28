#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <csignal>
#include <cstdlib>
#include "server/Server.hpp"
#include "logger/start_loggerd.hpp"
#include "logger/log_macros.hpp"

// 全局服务器指针
Server* g_server = nullptr;

int main() 
{
    // 启动日志守护进程
    StartLoggerDaemon();
    LOG_INFO("日志守护进程已启动");
    
    const int port = 1234;  
    LOG_INFO("服务器将监听端口: {}", port);

    try 
    {
        LOG_INFO("正在初始化服务器...");
        g_server = &Server::getInstance(port);
        LOG_INFO("服务器初始化完成，开始运行");
        g_server->start();
    } 
    catch (const std::exception& e) 
    {
        LOG_ERROR("服务器运行出错: {}", e.what());
        return 1;
    }

    return 0;
}