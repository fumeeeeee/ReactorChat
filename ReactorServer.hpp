#pragma once

#include "Reactor.hpp"
#include "protocol/Protocol.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>

// 前向声明
class ReactorServer;

// 客户端信息结构
struct Client
{
    std::string address;
    std::string name;
    bool isReceiving = false;
    int fd;
};

// 服务器监听器 - 处理新连接
class ServerAcceptor : public EventHandler
{
public:
    explicit ServerAcceptor(int listen_fd, ReactorServer *server);
    ~ServerAcceptor() override;

    void handleRead() override;
    void handleWrite() override;
    void handleError() override;
    int getFd() const override { return listen_fd_; }

private:
    int listen_fd_;
    ReactorServer *server_;
};

// 客户端连接处理器-处理客户端消息
class ClientHandler : public EventHandler
{
public:
    explicit ClientHandler(int client_fd, const std::string &address, ReactorServer *server);
    ~ClientHandler() override;

    void handleRead() override;
    void handleWrite() override;
    void handleError() override;
    int getFd() const override { return client_fd_; }

    // 客户端信息访问
    const std::string &getName() const { return client_.name; }
    const std::string &getAddress() const { return client_.address; }
    void setName(const std::string &name) { client_.name = name; }
    bool isNameSet() const { return !client_.name.empty(); }

    // 发送消息
    bool sendMessage(const std::vector<char> &message);

private:
    int client_fd_;
    Client client_;
    ReactorServer *server_;

    // 消息处理缓冲区
    std::vector<char> read_buffer_;
    std::queue<std::vector<char>> write_queue_;
    std::mutex write_mutex_;
    std::mutex read_buffer_mutex_;

    // 私有方法
    bool processMessages();
    bool handleCompleteMessage(const MSG_header &header, const std::string &msg);
    bool handleJoinMessage(const MSG_header &header);
    void handleGroupMessage(const MSG_header &header, const std::string &msg);
    void handleFileMessage(const MSG_header &header);
    void handleExitMessage();
    bool processFileTransfer();
    void cleanup();
};

// Reactor模式的聊天室服务器
class ReactorServer
{
public:
    explicit ReactorServer(int port = 1234, size_t thread_count = 0);
    ~ReactorServer();

    // 禁用拷贝构造和赋值
    ReactorServer(const ReactorServer &) = delete;
    ReactorServer &operator=(const ReactorServer &) = delete;

    // 服务器控制
    void start();
    void stop();
    bool isRunning() const;

    // 客户端管理
    void addClient(std::shared_ptr<ClientHandler> client);
    void removeClient(int client_fd);
    std::shared_ptr<ClientHandler> getClient(int client_fd);

    // 消息广播
    void broadcastMessage(const std::vector<char> &message, int exclude_fd = -1);
    void syncUserListForClient(int target_fd);
    // 获取在线用户列表
    std::vector<std::string> getOnlineUsers() const;

    // Reactor访问
    Reactor &getReactor() { return reactor_; }

private:
    int port_;
    int listen_fd_;

    // Reactor和线程池
    Reactor reactor_;
    std::shared_ptr<ThreadPool> thread_pool_;
    std::thread reactor_thread_;

    // 客户端管理
    std::unordered_map<int, std::shared_ptr<ClientHandler>> clients_;
    mutable std::mutex clients_mutex_;

    // 服务器监听器
    std::shared_ptr<ServerAcceptor> acceptor_;

    // 私有方法
    void initializeServer();
    void createListenSocket();
};