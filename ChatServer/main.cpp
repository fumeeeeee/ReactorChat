#include "reactor/ReactorServer.hpp"
#include "logger/log_macros.hpp"
#include <csignal>
#include <iostream>
#include <memory>
#include "logger/start_loggerd.hpp"

// 全局服务器指针，用于信号处理
extern std::unique_ptr<ReactorServer> g_server;
extern std::shared_ptr<AuthClient> g_authClient;
// 信号处理函数
void signalHandler(int signal)
{
    LOG_INFO("收到信号 {}, 开始关闭服务器...", signal);
    if (g_server)
    {
        g_server->stop();
    }
}

// 使用示例: ./chatserver 1234 4
// 其中1234是端口号,4是线程数,如果不指定线程数,则使用默认值(CPU核心数的两倍)
int main(int argc, char *argv[])
{
    StartLoggerDaemon();
    
    // 初始化 AuthClient
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("127.0.0.1:50051", grpc::InsecureChannelCredentials());
    g_authClient = std::make_shared<AuthClient>(channel);
    // 在 processAuthRequest 中调用 g_authClient -> Login(...) 或 Register(...)

    try
    {
        // 设置信号处理,注意,在编辑器的伪终端中,会拦截终止信号导致实际上无法调用signalHandler
        signal(SIGINT, signalHandler);  // Ctrl C信号
        signal(SIGTERM, signalHandler); // 终止信号
        signal(SIGPIPE, SIG_IGN);       // 进程向一个已经关闭的管道或套接字写入数据时,会触发 SIGPIPE,忽略SIGPIPE

        // 解析命令行参数
        int port = 1234;
        size_t thread_count = 0; // 0表示使用默认值
        // argv[0] 是程序名
        if (argc > 1)
        {
            port = std::atoi(argv[1]); //"ASCII to Integer" 将命令行参数转换为整数
            if (port <= 0 || port > 65535)
            {
                std::cerr << "无效的端口号: " << argv[1] << std::endl;
                return 1;
            }
        }

        if (argc > 2)
        {
            thread_count = std::atoi(argv[2]);
        }

        LOG_INFO("===== Reactor聊天室服务器准备启动 =====");
        LOG_INFO("将要监听端口: {}", port);
        // 三元表达式两个结果必须类型兼容
        LOG_INFO("将使用线程数: {}", thread_count == 0 ? "自动检测" : std::to_string(thread_count));

        // 创建并启动服务器
        g_server = std::make_unique<ReactorServer>(port, thread_count);
        g_server->start();

        // 主线程等待服务器运行
        std::cout << "服务器已启动，按下 Ctrl+C 停止服务器" << std::endl;
        std::cout << "服务器正在监听端口: " << port << std::endl;
        std::cout << "线程池大小为: " << (thread_count == 0 ? std::thread::hardware_concurrency() * 2 : thread_count) << std::endl;

        // 等待服务器停止
        g_server->waitStop();

        LOG_INFO("服务器已正常退出");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("服务器运行时发生异常: {}", e.what());
        std::cerr << "服务器异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
