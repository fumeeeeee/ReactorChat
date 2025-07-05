#include "ReactorServer.hpp"
#include "ClientHandler.hpp"
#include "threadpool/ThreadPool.hpp"
#include "logger/log_macros.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <queue>
#include <chrono>

// ReactorServer初始化线程池
ReactorServer::ReactorServer(int port, size_t thread_count)
    : port_(port), listen_fd_(-1)
{
    if (thread_count == 0)
    {
        thread_count = std::thread::hardware_concurrency() * 2;
        if (thread_count == 0)
            thread_count = 4;
    }
    thread_pool_ = std::make_shared<ThreadPool>(thread_count);
    reactor_.setThreadPool(thread_pool_);
    LOG_DEBUG("ReactorServer初始化，端口: {}, 线程数: {}", port_, thread_count);
}

ReactorServer::~ReactorServer()
{
    stop();
}

void ReactorServer::start()
{
    try
    {
        initializeServer(); // 初始化监听socket,初始化reactor,注册ServerAcceptor到reactor
        // 创建单线程reactor并运行
        reactor_thread_ = std::thread([this]()
                                      { reactor_.run(); });
        LOG_INFO("ReactorServer启动成功，监听端口: {}", port_);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("启动服务器失败: {}", e.what());
        throw;
    }
}

void ReactorServer::stop()
{
    LOG_INFO("正在停止ReactorServer...");
    reactor_.stop();
    if (reactor_thread_.joinable())
    {
        reactor_thread_.join();
    }
    if (listen_fd_ >= 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.clear();
    }

    // 通知main线程停止
    {
        std::lock_guard<std::mutex> lock(mtx);
        running = false;
    }
    {
        std::lock_guard<std::mutex> lock(mtx);
        running = false;
    }
    cv.notify_all();
    LOG_INFO("ReactorServer已停止");
}

bool ReactorServer::isRunning() const
{
    return reactor_.isRunning();
}

void ReactorServer::addClient(std::shared_ptr<ClientHandler> client)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_[client->getFd()] = client;
}

void ReactorServer::removeClient(int client_fd)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_.find(client_fd);
    if (it != clients_.end())
    {
        // 从Reactor中移除事件处理器
        reactor_.removeHandler(client_fd);
        // 从客户端映射中移除
        clients_.erase(it);
        LOG_INFO("客户端 (fd: {}) 已被移除", client_fd);
    }
}

std::shared_ptr<ClientHandler> ReactorServer::getClient(int client_fd)
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_.find(client_fd);
    return (it != clients_.end()) ? it->second : nullptr;
}

void ReactorServer::broadcastMessage(const std::vector<char> &message, int exclude_fd)
{
    std::vector<std::shared_ptr<ClientHandler>> clients_copy;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto &pair : clients_)
        {
            if (pair.first != exclude_fd)
            {
                clients_copy.push_back(pair.second);
            }
        }
    }

    LOG_DEBUG("广播消息给 {} 个客户端，消息总大小: {} 字节",
              clients_copy.size(), message.size());

    size_t success_count = 0;
    for (auto &client : clients_copy)
    {
        if (client->sendMessage(message))
        {
            success_count++;
        }
    }

    LOG_DEBUG("成功发送给 {}/{} 个客户端", success_count, clients_copy.size());
}

void ReactorServer::syncUserListForClient(int target_fd)
{
    auto target_client = getClient(target_fd);
    if (!target_client)
        return;

    std::vector<std::string> users;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto &pair : clients_)
        {
            // 列表包含除自己外的所有已登录用户
            if (pair.first != target_fd && pair.second->isNameSet())
            {
                users.push_back(pair.second->getName());
            }
        }
    }

    if (users.empty())
        return;

    std::ostringstream oss;
    for (size_t i = 0; i < users.size(); ++i)
    {
        oss << users[i] << (i == users.size() - 1 ? "" : ",");
    }
    std::string user_list = oss.str();

    target_client->sendMessage(encodeMessage(INITIAL, user_list, "SERVER"));

    LOG_INFO("向 {} (fd: {}) 发送用户列表: [{}]", target_client->getName(), target_fd, user_list);
}

void ReactorServer::initializeServer()
{
    createListenSocket();
    acceptor_ = std::make_shared<ServerAcceptor>(listen_fd_, this);
    if (!reactor_.registerHandler(acceptor_, EventType::READ))
    {
        throw std::runtime_error("注册服务器接受器失败");
    }
}

void ReactorServer::createListenSocket()
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        throw std::runtime_error("创建监听socket失败: " + std::string(strerror(errno)));
    }
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        close(listen_fd_);
        throw std::runtime_error("设置socket选项失败: " + std::string(strerror(errno)));
    }
    int flags = fcntl(listen_fd_, F_GETFL, 0);
    if (fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        close(listen_fd_);
        throw std::runtime_error("设置非阻塞模式失败: " + std::string(strerror(errno)));
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    if (bind(listen_fd_, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        close(listen_fd_);
        throw std::runtime_error("绑定地址失败: " + std::string(strerror(errno)));
    }
    if (listen(listen_fd_, SOMAXCONN) < 0)
    {
        close(listen_fd_);
        throw std::runtime_error("监听失败: " + std::string(strerror(errno)));
    }
    LOG_DEBUG("创建监听socket成功，fd: {}, port: {}", listen_fd_, port_);
}

void ReactorServer::waitStop()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this]
            { return !running; });
}
