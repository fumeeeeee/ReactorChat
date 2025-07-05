#pragma once

#include "Reactor.hpp"
#include "protocol/Protocol.hpp"
#include "ServerAcceptor.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>
#include <thread>
#include <condition_variable>

// 前向声明
class ClientHandler;
class ServerAcceptor;

class ReactorServer
{
public:
    ReactorServer(int port, size_t thread_count = 0);
    ~ReactorServer();

    // 禁用拷贝构造和赋值
    ReactorServer(const ReactorServer &) = delete;
    ReactorServer &operator=(const ReactorServer &) = delete;

    // 服务器控制
    void start();
    void stop();
    bool isRunning() const;
    void waitStop();

    // 客户端管理
    void addClient(std::shared_ptr<ClientHandler> client);
    void removeClient(int client_fd);
    std::shared_ptr<ClientHandler> getClient(int client_fd);

    // 消息广播
    void broadcastMessage(const std::vector<char> &message, int exclude_fd = -1);
    void syncUserListForClient(int target_fd);
    // Reactor访问
    Reactor &getReactor() { return reactor_; }

private:
    void initializeServer();
    void createListenSocket();

    int port_;
    int listen_fd_;

    // Reactor和线程池
    Reactor reactor_;
    std::shared_ptr<ThreadPool> thread_pool_;
    std::thread reactor_thread_;

    // 维护在线客户映射表fd到ClientHandler的映射
    std::unordered_map<int, std::shared_ptr<ClientHandler>> clients_;
    mutable std::mutex clients_mutex_;

    // 服务器监听器
    std::shared_ptr<ServerAcceptor> acceptor_;

    // 用于通知main主线程退出
    bool running = true;
    std::mutex mtx;
    std::condition_variable cv;
};