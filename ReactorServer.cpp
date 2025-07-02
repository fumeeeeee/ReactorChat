#include "ReactorServer.hpp"
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

// ServerAcceptor实现
ServerAcceptor::ServerAcceptor(int listen_fd, ReactorServer *server)
    : listen_fd_(listen_fd), server_(server)
{
    LOG_DEBUG("创建ServerAcceptor，fd: {}", listen_fd_);
}

ServerAcceptor::~ServerAcceptor()
{
    LOG_DEBUG("销毁ServerAcceptor，fd: {}", listen_fd_);
}

void ServerAcceptor::handleRead()
{
    // 边缘触发模式，需要循环接受所有连接
    while (true)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd_, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // 没有更多连接
            }
            LOG_ERROR("接受连接失败: {}", strerror(errno));
            return;
        }

        // 设置非阻塞模式
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            LOG_ERROR("设置客户端非阻塞模式失败: {}", strerror(errno));
            close(client_fd);
            continue;
        }

        // 生成客户端地址字符串
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, INET_ADDRSTRLEN);
        std::string address = std::string(addr_str) + ":" + std::to_string(ntohs(client_addr.sin_port));

        // 创建客户端处理器
        auto client_handler = std::make_shared<ClientHandler>(client_fd, address, server_);

        // 注册到Reactor
        if (server_->getReactor().registerHandler(client_handler, EventType::READ))
        {
            server_->addClient(client_handler);
            LOG_INFO("新客户端连接: {} (fd: {})", address, client_fd);
        }
        else
        {
            LOG_ERROR("注册客户端处理器失败，fd: {}", client_fd);
            close(client_fd);
        }
    }
}

void ServerAcceptor::handleWrite()
{
    LOG_WARN("ServerAcceptor收到写事件，这不应该发生");
}

void ServerAcceptor::handleError()
{
    LOG_ERROR("ServerAcceptor发生错误，fd: {}", listen_fd_);
}

// ClientHandler实现 
ClientHandler::ClientHandler(int client_fd, const std::string &address, ReactorServer *server)
    : client_fd_(client_fd),
      server_(server),
      read_buffer_(),
      write_queue_(),
      write_mutex_(),
      read_buffer_mutex_()
{
    client_.fd = client_fd; 
    client_.address = address; 
    client_.isReceiving = false; 

    LOG_DEBUG("创建ClientHandler，fd: {}, address: {}", client_fd_, address);
}

ClientHandler::~ClientHandler()
{
    cleanup();
    LOG_DEBUG("销毁ClientHandler，fd: {}", client_fd_);
}

void ClientHandler::handleRead()
{
    // 1：IO操作-尽可能多地读取数据
    char buffer[8192]; 
    bool has_new_data = false;
    
    while (true) {
        ssize_t bytes_read = recv(client_fd_, buffer, sizeof(buffer), 0);
        if (bytes_read > 0) {
            {
                std::lock_guard<std::mutex> lock(read_buffer_mutex_);
                read_buffer_.insert(read_buffer_.end(), buffer, buffer + bytes_read);
            }
            has_new_data = true;
            LOG_DEBUG("读取到 {} 字节数据，缓冲区总大小: {}", 
                      bytes_read, read_buffer_.size());
        }
        else if (bytes_read == 0) {
            LOG_DEBUG("客户端正常关闭连接");
            handleError();
            return;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有更多数据可读
            }
            LOG_ERROR("读取数据失败: {}", strerror(errno));
            handleError();
            return;
        }
    }
    
    if (!has_new_data) {
        LOG_DEBUG("本次handleRead没有新数据");
        return;
    }

    // 2：处理缓冲区中的数据
    int process_rounds = 0;
    const int MAX_PROCESS_ROUNDS = 10; // 防止无限循环
    
    while (process_rounds < MAX_PROCESS_ROUNDS) {
        size_t buffer_size_before;
        {
            std::lock_guard<std::mutex> lock(read_buffer_mutex_);
            buffer_size_before = read_buffer_.size();
        }
        
        if (buffer_size_before == 0) {
            break; // 缓冲区已空
        }

        bool processing_result;
        if (client_.isReceiving) {
            // 在文件传输状态下，直接处理数据，不尝试解析消息头
            processing_result = processFileTransfer();
        } else {
            // 非文件传输状态下，尝试解析消息头
            if (read_buffer_.size() >= sizeof(MSG_header)) {
                MSG_header header;
                memcpy(&header, read_buffer_.data(), sizeof(header));
                if (header.Type == FILE_MSG) {
                    // 如果是文件传输请求，设置状态并处理
                    client_.isReceiving = true;
                    handleFileMessage(header);
                    processing_result = true;
                } else {
                    processing_result = processMessages();
                }
            } else {
                // 数据不足，等待更多数据
                LOG_DEBUG("缓冲区数据不足，等待更多数据");
                break;
            }
        }
        
        if (!processing_result) {
            handleError();
            return;
        }
        
        size_t buffer_size_after;
        {
            std::lock_guard<std::mutex> lock(read_buffer_mutex_);
            buffer_size_after = read_buffer_.size();
        }
        
        // 如果缓冲区大小没有变化，说明是半包数据，退出循环
        if (buffer_size_after == buffer_size_before) {
            LOG_DEBUG("缓冲区大小未变化，可能是半包数据，等待更多数据");
            break;
        }
        
        process_rounds++;
    }
    
    if (process_rounds >= MAX_PROCESS_ROUNDS) {
        LOG_WARN("数据处理轮次达到上限，可能存在问题");
    }
}

bool ClientHandler::processMessages()
{
    // 从 read_buffer_ 中循环解析出完整的消息包
    while (true)
    {
        MSG_header header;
        std::string msg_content;

        { // 加锁以安全地访问和修改read_buffer_
            std::lock_guard<std::mutex> lock(read_buffer_mutex_);

            if (read_buffer_.size() < sizeof(MSG_header))
            {
                break; // 缓冲区数据不足以解析出一个完整的消息头
            }

            // 预读取消息头
            memcpy(&header, read_buffer_.data(), sizeof(header));
            size_t total_size = sizeof(header) + header.length;

            if (read_buffer_.size() < total_size)
            {
                break; // 缓冲区数据不足以解析出一个完整的消息包
            }

            // 提取消息内容
            if (header.length > 0)
            {
                msg_content.assign(read_buffer_.begin() + sizeof(header),
                                   read_buffer_.begin() + total_size);
            }

            // 从缓冲区移除已处理的完整消息
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + total_size);
        }

        // 在锁外处理解析出的完整消息
        if (!handleCompleteMessage(header, msg_content))
        {
            return false; // 消息处理函数要求断开连接
        }
    }
    return true;
}

void ClientHandler::handleWrite()
{
    std::lock_guard<std::mutex> lock(write_mutex_);

    while (!write_queue_.empty())
    {
        const auto &message = write_queue_.front();

        ssize_t sent = send(client_fd_, message.data(), message.size(), MSG_NOSIGNAL);
        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // 发送缓冲区满，稍后重试
            }
            LOG_ERROR("发送消息失败，fd: {}, error: {}", client_fd_, strerror(errno));
            handleError();
            return;
        }

        if (static_cast<size_t>(sent) == message.size())
        {
            write_queue_.pop();
        }
        else
        {
            // 部分发送，修改队列中的消息
            auto &front_msg = const_cast<std::vector<char> &>(write_queue_.front());
            front_msg.erase(front_msg.begin(), front_msg.begin() + sent);
            break;
        }
    }

    // 如果写队列为空且之前注册了写事件，移除写事件
    if (write_queue_.empty())
    {
        server_->getReactor().modifyHandler(client_fd_, EventType::READ);
    }
}

void ClientHandler::handleError()
{
    LOG_INFO("客户端连接异常或断开: {} (fd: {})", client_.address, client_fd_);

    std::string client_name = client_.name; // 备份客户端名称
    int current_fd = client_fd_;

    // 步骤1: 从服务器核心数据结构中移除此客户端
    // 这可以防止在广播退出消息时，把消息发给一个即将被销毁的handler
    server_->removeClient(current_fd);

    // 步骤2: 如果客户端已经登录（有名字），则广播其退出消息
    if (!client_name.empty())
    {
        MSG_header header;
        header.Type = EXIT;
        header.length = 0;
        std::strncpy(header.sender_name, client_name.c_str(), sizeof(header.sender_name) - 1);
        header.sender_name[sizeof(header.sender_name) - 1] = '\0';

        std::vector<char> exit_message(sizeof(header));
        memcpy(exit_message.data(), &header, sizeof(header));

        server_->broadcastMessage(exit_message, -1); // 广播给所有剩余的客户端
    }
    
    // cleanup() 会被析构函数调用，这里无需手动调用
}

bool ClientHandler::sendMessage(const std::vector<char> &message)
{
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(message);

    // 注册写事件，以便Reactor在socket可写时通知我们
    server_->getReactor().modifyHandler(client_fd_,
                                        static_cast<EventType>(static_cast<uint32_t>(EventType::READ) | static_cast<uint32_t>(EventType::WRITE)));
    return true;
}


bool ClientHandler::handleCompleteMessage(const MSG_header &header, const std::string &msg)
{
    LOG_DEBUG("处理消息 - 类型: {}, 发送者: {}, 长度: {}",
              static_cast<int>(header.Type), header.sender_name, header.length);

    switch (header.Type)
    {
    case INITIAL: // 该消息类型在旧版中用于用户列表，新版逻辑中主要用于JOIN时处理
        // 在新版中，客户端初次连接后应发送JOIN消息来设置名称
        LOG_WARN("收到未预期的INITIAL消息类型");
        break;
    case JOIN:
        return handleJoinMessage(header); 
    case GROUP_MSG:
        handleGroupMessage(header, msg); 
        break;
    case FILE_MSG:
        handleFileMessage(header); 
        break;
    case EXIT:
        handleExitMessage(); 
        return false;
    case FAILED:
        LOG_WARN("收到来自客户端的FAILED消息");
        break;
    default:
        LOG_WARN("未知消息类型: {}", static_cast<int>(header.Type));
        break;
    }
    return true;
}

bool ClientHandler::handleJoinMessage(const MSG_header &header)
{
    std::string username = header.sender_name;

    // 检查用户名是否已存在
    auto online_users = server_->getOnlineUsers();
    if (std::find(online_users.begin(), online_users.end(), username) != online_users.end())
    {
        LOG_WARN("用户 {} 尝试使用已存在的名称 '{}'", client_.address, username);
        MSG_header error_header;
        error_header.Type = FAILED;
        std::string error_msg = "Username already exists.";
        error_header.length = error_msg.length();
        std::strncpy(error_header.sender_name, "Server", sizeof(error_header.sender_name) - 1);
        error_header.sender_name[sizeof(error_header.sender_name) - 1] = '\0';

        std::vector<char> response(sizeof(error_header) + error_header.length);
        memcpy(response.data(), &error_header, sizeof(error_header));
        memcpy(response.data() + sizeof(error_header), error_msg.c_str(), error_msg.length());

        sendMessage(response);
        return false; // 返回false，表示需要断开此客户端
    }

    // 设置客户端名称
    client_.name = username;
    LOG_INFO("客户端 {} (fd: {}) 设置名称为: {}", client_.address, client_fd_, client_.name);

    // 1. 发送在线用户列表给新用户
    server_->syncUserListForClient(getFd());

    // 2. 广播新用户加入的消息给其他所有用户
    MSG_header join_header;
    join_header.Type = JOIN;
    join_header.length = 0;
    std::strncpy(join_header.sender_name, username.c_str(), sizeof(join_header.sender_name) - 1);
    join_header.sender_name[sizeof(join_header.sender_name) - 1] = '\0';

    std::vector<char> join_message(sizeof(join_header));
    memcpy(join_message.data(), &join_header, sizeof(join_header));

    server_->broadcastMessage(join_message, client_fd_);

    return true;
}

void ClientHandler::handleGroupMessage(const MSG_header &header, const std::string &msg)
{
    if (client_.name.empty())
    {
        LOG_WARN("未设置名称的客户端 {} 尝试发送群消息", client_.address);
        return;
    }
    LOG_INFO("转发群消息: {} -> {}", client_.name, msg);

    // 重新打包消息头和消息体进行广播
    size_t total_size = sizeof(header) + header.length;
    std::vector<char> message(total_size);
    memcpy(message.data(), &header, sizeof(header));
    memcpy(message.data() + sizeof(header), msg.c_str(), msg.length());

    server_->broadcastMessage(message, client_fd_);
}

void ClientHandler::handleFileMessage(const MSG_header &header)
{
    if (client_.name.empty()) {
        LOG_WARN("未设置名称的客户端 {} 尝试发送文件", client_.address);
        return;
    }
    
    LOG_INFO("开始处理文件传输请求: 发送者={}, 文件大小={}", 
             client_.name, header.length);

    // 1. 设置客户端为接收文件状态
    client_.isReceiving = true;
    
    // 2. 广播文件头给其他客户端
    std::vector<char> header_message(sizeof(header));
    memcpy(header_message.data(), &header, sizeof(header));
    server_->broadcastMessage(header_message, client_fd_);

    // 3. 清空read_buffer_，确保后续数据是纯文件数据
    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);
        read_buffer_.clear();
    }
}

bool ClientHandler::processFileTransfer()
{
    if (!client_.isReceiving) {
        return true;  // 不在接收文件状态，继续处理其他消息
    }

    // 从read_buffer_中读取数据
    std::lock_guard<std::mutex> lock(read_buffer_mutex_);
    
    if (read_buffer_.empty()) {
        return true;  // 没有数据可处理
    }

    // 获取当前缓冲区大小
    size_t buffer_size = read_buffer_.size();
    
    // 直接转发数据给其他客户端，不做任何处理
    std::vector<char> data_chunk(read_buffer_.begin(), read_buffer_.end());
    server_->broadcastMessage(data_chunk, client_fd_);
    
    // 清空缓冲区
    read_buffer_.clear();
    
    LOG_DEBUG("处理文件传输数据: {} 字节", buffer_size);
    
    return true;
}

void ClientHandler::handleExitMessage()
{
    LOG_INFO("客户端 {} 请求退出", client_.name);
    // handleError()会处理所有退出逻辑，所以这里不需要做额外的事
}

void ClientHandler::cleanup()
{
    if (client_fd_ >= 0)
    {
        // 确保socket被关闭
        close(client_fd_);
        client_fd_ = -1;
    }
}


// ReactorServer实现 
ReactorServer::ReactorServer(int port, size_t thread_count)
    : port_(port), listen_fd_(-1)
{
    if (thread_count == 0)
    {
        thread_count = std::thread::hardware_concurrency() * 2;
        if (thread_count == 0) thread_count = 4;
    }
    thread_pool_ = std::make_shared<ThreadPool>(thread_count);
    reactor_.setThreadPool(thread_pool_);
    LOG_INFO("ReactorServer初始化，端口: {}, 线程数: {}", port_, thread_count);
}

ReactorServer::~ReactorServer()
{
    stop();
}

void ReactorServer::start()
{
    try
    {
        initializeServer();
        reactor_thread_ = std::thread([this]() { reactor_.run(); });
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
        // 从Reactor中移除事件处理器，这很重要
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
        for (const auto &pair : clients_) {
            if (pair.first != exclude_fd) {
                clients_copy.push_back(pair.second);
            }
        }
    }

    LOG_DEBUG("广播消息给 {} 个客户端，消息大小: {} 字节", 
              clients_copy.size(), message.size());

    size_t success_count = 0;
    for (auto &client : clients_copy) {
        if (client->sendMessage(message)) {
            success_count++;
        }
    }
    
    LOG_DEBUG("成功发送给 {}/{} 个客户端", success_count, clients_copy.size());
}

// 为单个客户端同步用户列表的函数
void ReactorServer::syncUserListForClient(int target_fd)
{
    auto target_client = getClient(target_fd);
    if (!target_client) return;

    std::vector<std::string> users;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& pair : clients_) {
            // 列表包含除自己外的所有已登录用户
            if (pair.first != target_fd && pair.second->isNameSet()) {
                users.push_back(pair.second->getName());
            }
        }
    }

    if (users.empty()) return;

    std::ostringstream oss;
    for (size_t i = 0; i < users.size(); ++i) {
        oss << users[i] << (i == users.size() - 1 ? "" : ",");
    }
    std::string user_list = oss.str();

    MSG_header header;
    header.Type = INITIAL; // 使用INITIAL类型发送用户列表
    header.length = user_list.length();
    std::strncpy(header.sender_name, "SERVER", sizeof(header.sender_name) - 1);
    header.sender_name[sizeof(header.sender_name) - 1] = '\0';

    std::vector<char> message(sizeof(header) + header.length);
    memcpy(message.data(), &header, sizeof(header));
    memcpy(message.data() + sizeof(header), user_list.c_str(), user_list.length());

    target_client->sendMessage(message);
    LOG_INFO("向 {} (fd: {}) 发送用户列表: [{}]", target_client->getName(), target_fd, user_list);
}


std::vector<std::string> ReactorServer::getOnlineUsers() const
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<std::string> users;
    for (const auto &pair : clients_)
    {
        if (pair.second->isNameSet())
        {
            users.push_back(pair.second->getName());
        }
    }
    return users;
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
    LOG_INFO("创建监听socket成功，fd: {}, port: {}", listen_fd_, port_);
}