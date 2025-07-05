#include "AuthServiceImpl.hpp"
#include <grpcpp/grpcpp.h>
#include <signal.h>
#include <memory>
#include <thread>

// 全局服务器指针
std::unique_ptr<grpc::Server> g_server;
std::atomic<bool> shutdown_requested(false);

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        shutdown_requested = true; // 只设标志
    }
}

void RunServer()
{
    std::string server_address("0.0.0.0:50051");

    // 尽可能使用智能指针管理服务对象的生命周期
    auto service = std::make_unique<AuthServiceImpl>("tcp://127.0.0.1:3306", "root", "2431378182", "my_chat_db");

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // 向 grpc::ServerBuilder 注册服务
    // std::unique_ptr 或 std::shared_ptr 是封装指针的智能指针类型，不能直接传给 RegisterService，必须使用 .get() 提取出内部的原始指针
    builder.RegisterService(service.get());

    g_server = builder.BuildAndStart();

    if (!g_server)
    {
        std::cerr << "服务器启动失败!" << std::endl;
        return;
    }

    std::cout << "Authentication服务器启动于 " << server_address << std::endl;

    // 设置信号处理器
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Server::Wait() 直到Server::Shutdown()调用完成才会执行
    // wait本身内部使用了 absl::Mutex（internal_mutex）来协调服务的生命周期
    // 比如：检查活跃 RPC 数量；等待所有线程退出；配合 Shutdown() 协调状态

    // 如果在signal handler 中调用了 grpc::Server::Shutdown()，这个函数内部会试图加锁一个当前线程Wait已经持有的 absl::Mutex，从而导致了死锁检测
    // 避免在信号处理函数中直接执行复杂的、可能阻塞或涉及互斥锁的操作。信号处理函数应该是异步安全的
    while (!shutdown_requested)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "收到关闭信号，正在关闭服务器..." << std::endl;
    g_server->Shutdown(); //通知 gRPC 服务器关闭服务
    g_server->Wait(); //阻塞主线程，直到所有活动的 RPC 完成即 Shutdown 完成

    std::cout << "Authentication服务器已关闭" << std::endl;
}

int main()
{
    try
    {
        RunServer();
    }
    catch (const std::exception &e)
    {
        std::cerr << "服务器运行时发生错误: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}