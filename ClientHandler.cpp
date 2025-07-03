#include "ClientHandler.hpp"
#include "ReactorServer.hpp"
#include "logger/log_macros.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <algorithm>

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
    // 步骤1：I/O操作 - 尽可能多地读取数据
    char buffer[8192];
    bool has_new_data = false;

    while (true)
    {
        ssize_t bytes_read = recv(client_fd_, buffer, sizeof(buffer), 0);
        if (bytes_read > 0)
        {
            {
                std::lock_guard<std::mutex> lock(read_buffer_mutex_);
                read_buffer_.insert(read_buffer_.end(), buffer, buffer + bytes_read);
            }
            has_new_data = true;
            LOG_DEBUG("读取到 {} 字节数据，缓冲区总大小: {}",
                      bytes_read, read_buffer_.size());
        }
        else if (bytes_read == 0)
        {
            LOG_DEBUG("客户端正常关闭连接");
            handleError();
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // 没有更多数据可读
            }
            LOG_ERROR("读取数据失败: {}", strerror(errno));
            handleError();
            return;
        }
    }

    if (!has_new_data)
    {
        LOG_DEBUG("本次handleRead没有新数据");
        return;
    }

    // 步骤2：处理缓冲区中的数据
    int process_rounds = 0;
    const int MAX_PROCESS_ROUNDS = 10; // 防止无限循环

    while (process_rounds < MAX_PROCESS_ROUNDS)
    {
        size_t buffer_size_before;
        {
            std::lock_guard<std::mutex> lock(read_buffer_mutex_);
            buffer_size_before = read_buffer_.size();
        }

        if (buffer_size_before == 0)
        {
            break; // 缓冲区已空
        }

        bool processing_result;
        if (client_.isReceiving)
        {
            // 在文件传输状态下，直接处理数据，不尝试解析消息头
            processing_result = processFileTransfer();
        }
        else
        {
            // 非文件传输状态下，尝试解析消息头
            if (read_buffer_.size() >= sizeof(MSG_header))
            {
                MSG_header header;
                memcpy(&header, read_buffer_.data(), sizeof(header));
                if (header.Type == FILE_MSG)
                {
                    // 如果是文件传输请求，设置状态并处理
                    client_.isReceiving = true;
                    handleFileMessage(header);
                    processing_result = true;
                }
                else
                {
                    processing_result = processMessages();
                }
            }
            else
            {
                // 数据不足，等待更多数据
                LOG_DEBUG("缓冲区数据不足，等待更多数据");
                break;
            }
        }

        if (!processing_result)
        {
            handleError();
            return;
        }

        size_t buffer_size_after;
        {
            std::lock_guard<std::mutex> lock(read_buffer_mutex_);
            buffer_size_after = read_buffer_.size();
        }

        // 如果缓冲区大小没有变化，说明是半包数据，退出循环
        if (buffer_size_after == buffer_size_before)
        {
            LOG_DEBUG("缓冲区大小未变化，可能是半包数据，等待更多数据");
            break;
        }

        process_rounds++;
    }

    if (process_rounds >= MAX_PROCESS_ROUNDS)
    {
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

        { 
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

    // 步骤2: 如果客户端已经登录，则广播其退出消息
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
        // 客户端初次连接后应发送JOIN消息来设置名称
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
    if (client_.name.empty())
    {
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
    if (!client_.isReceiving)
    {
        return true; // 不在接收文件状态，继续处理其他消息
    }

    // 从read_buffer_中读取数据
    std::lock_guard<std::mutex> lock(read_buffer_mutex_);

    if (read_buffer_.empty())
    {
        return true; // 没有数据可处理
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