#include "ClientHandler.hpp"
#include "ReactorServer.hpp"
#include "logger/log_macros.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <algorithm>

extern std::shared_ptr<AuthClient> g_authClient;

ClientHandler::ClientHandler(int client_fd, const std::string &address, ReactorServer *server)
    : client_fd_(client_fd),
      server_(server),
      read_buffer_(),
      write_queue_(),
      write_mutex_(),
      read_buffer_mutex_(),
      is_receiving_file_(false),
      current_file_info_()
{
    client_.fd = client_fd;
    client_.address = address;

    LOG_DEBUG("创建ClientHandler，fd: {}, address: {}", client_fd_, address);
}

ClientHandler::~ClientHandler()
{
    cleanup();
    LOG_DEBUG("销毁ClientHandler，fd: {}", client_fd_);
}

void ClientHandler::handleRead()
{
    constexpr size_t TEMP_BUFFER_SIZE = 512 * 1024;
    bool has_new_data = false;

    while (true)
    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);

        // 在 read_buffer_ 尾部预留空间
        size_t old_size = read_buffer_.size();
        read_buffer_.resize(old_size + TEMP_BUFFER_SIZE);
        char* write_ptr = read_buffer_.data() + old_size;

        ssize_t bytes_read = recv(client_fd_, write_ptr, TEMP_BUFFER_SIZE, 0);
        if (bytes_read > 0)
        {
            // 按实际读取量修正 buffer 尾部
            read_buffer_.resize(old_size + bytes_read);
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
            // 回滚 resize
            read_buffer_.resize(old_size);

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 非阻塞 socket 读取完毕
                break;
            }

            LOG_ERROR("读取数据失败: {}", strerror(errno));
            handleError();
            return;
        }
    }

    if (!has_new_data)
    {
        LOG_DEBUG("本次ClientHandleRead没有拿到新数据");
        return;
    }

    // 2. 处理消息
    processMessages();
}


void ClientHandler::processMessages()
{
    // 处理消息循环,每次循环处理一条消息
    while (true)
    {
        size_t buffer_size_before;
        {
            std::lock_guard<std::mutex> lock(read_buffer_mutex_);
            buffer_size_before = read_buffer_.size();
        }

        if (buffer_size_before == 0)
        {
            break;
        }
        // 3. 处理一条消息
        bool processing_result = processOneMessage();
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

        if (buffer_size_after == buffer_size_before)
        {
            LOG_DEBUG("缓冲区大小未变化，等待更多数据");
            break;
        }
    }

    // 检查内核缓冲区是否还有数据
    int bytesAvailable = 0;

    // ioctl 是一个设备控制函数，可以对文件描述符（这里是套接字 client_fd_）执行各种控制操作,调用成功后返回0
    // FIONREAD 是请求命令，意思是“查询当前套接字输入缓冲区中可读取的字节数”。
    // &bytesAvailable 是输出参数，同时会被写入当前可读取的字节数。
    // 这里会处理在我们处理消息时新到达的数据

    if (ioctl(client_fd_, FIONREAD, &bytesAvailable) == 0 && bytesAvailable > 0)
    {
        handleRead();
    }
}

bool ClientHandler::processOneMessage()
{
    // 4. processOneMessage 只读取消息头并根据类型进行分发
    MSG_header header;

    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);

        // 检查是否有足够的数据读取消息头
        if (read_buffer_.size() < sizeof(MSG_header))
        {
            LOG_DEBUG("缓冲区数据不足以读取消息头，等待更多数据");
            return true; // 不是错误，只是需要更多数据
        }

        // 读取消息头
        memcpy(&header, read_buffer_.data(), sizeof(header));
    }

    std::string sender(header.sender_name, strnlen(header.sender_name, sizeof(header.sender_name)));

    LOG_DEBUG("读取到消息头 - 类型: {}, 发送者: {}, 长度: {}",
              getMessageTypeName(header.Type), sender, header.length);

    // 根据消息类型处理
    switch (header.Type)
    {
    case FILE_MSG:
        return handleFileStartMessage(header);
    case FILE_DATA:
        return handleFileDataMessage(header);
    case FILE_END:
        return handleFileEndMessage(header);
    default:
        return handleRegularMessage(header);
    }
}

bool ClientHandler::handleRegularMessage(const MSG_header &header)
{
    // 5. handleRegularMessage 处理常规消息类型
    std::string msg_content;

    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);

        size_t total_message_size = sizeof(MSG_header) + header.length;
        if (read_buffer_.size() < total_message_size)
        {
            LOG_DEBUG("缓冲区数据不足以读取完整消息，需要 {} 字节，当前有 {} 字节",
                      total_message_size, read_buffer_.size());
            return true; // 等待更多数据
        }

        // 读取消息内容
        if (header.length > 0)
        {
            msg_content.assign(read_buffer_.begin() + sizeof(MSG_header),
                               read_buffer_.begin() + total_message_size);
        }

        // 从缓冲区移除已处理的消息
        read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + total_message_size);
    }

    // 6. 处理完整的消息进行二次分发
    return handleCompleteMessage(header, msg_content);
}

bool ClientHandler::handleFileStartMessage(const MSG_header &header)
{
    std::lock_guard<std::mutex> file_lock(file_receive_mutex_);
    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);

        size_t total_message_size = sizeof(MSG_header) + header.length;
        if (read_buffer_.size() < total_message_size)
        {
            LOG_DEBUG("缓冲区数据不足以读取文件开始消息");
            return true; // 等待更多数据
        }

        // 读取文件信息
        if (header.length == sizeof(FileInfo))
        {
            FileInfo file_info;
            memcpy(&file_info, read_buffer_.data() + sizeof(MSG_header), sizeof(FileInfo));

            // 设置文件传输状态
            is_receiving_file_ = true;
            current_file_info_ = file_info;

            LOG_INFO("开始文件传输: 发送者={}, 文件名={}, 文件大小={}",
                     header.sender_name, file_info.filename, file_info.file_size);
        }
        else
        {
            LOG_ERROR("FILE_MSG消息长度不正确: {}, 期望: {}", header.length, sizeof(FileInfo));
            // 从缓冲区移除错误的消息
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + total_message_size);
            return true;
        }

        // 从缓冲区移除已处理的消息
        read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + total_message_size);
    }

    // 广播文件开始消息给其他客户端
    server_->broadcastMessage(encodeFileStartMessage(header.sender_name,
                                                     current_file_info_.filename,
                                                     current_file_info_.file_size),
                              client_fd_);
    return true;
}

bool ClientHandler::handleFileDataMessage(const MSG_header &header)
{
    std::lock_guard<std::mutex> file_lock(file_receive_mutex_);
    if (!is_receiving_file_)
    {
        LOG_WARN("收到FILE_DATA消息但未处于文件接收状态");
        // 跳过这个消息
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);
        size_t total_message_size = sizeof(MSG_header) + header.length;
        if (read_buffer_.size() >= total_message_size)
        {
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + total_message_size);
        }
        return true;
    }

    std::vector<char> data_chunk;

    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);

        size_t total_message_size = sizeof(MSG_header) + header.length;
        if (read_buffer_.size() < total_message_size)
        {
            LOG_DEBUG("缓冲区数据不足以读取完整的文件数据消息");
            return true; // 等待更多数据
        }

        // 读取文件数据
        if (header.length > 0)
        {
            data_chunk.assign(read_buffer_.begin() + sizeof(MSG_header),
                              read_buffer_.begin() + total_message_size);
        }

        // 从缓冲区移除已处理的消息
        read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + total_message_size);
    }

    LOG_DEBUG("接收并转发文件数据块 {} 字节，来自 {}", data_chunk.size(), header.sender_name);

    // 转发文件数据给其他客户端，使用正确的发送者名称
    server_->broadcastMessage(encodeFileDataMessage(header.sender_name, data_chunk), client_fd_);

    return true;
}

bool ClientHandler::handleFileEndMessage(const MSG_header &header)
{
    std::lock_guard<std::mutex> file_lock(file_receive_mutex_);
    if (!is_receiving_file_)
    {
        LOG_WARN("收到FILE_END消息但未处于文件接收状态");
        // 跳过这个消息头
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);
        if (read_buffer_.size() >= sizeof(MSG_header))
        {
            read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + sizeof(MSG_header));
        }
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);

        // FILE_END消息只有头部，没有数据部分
        if (read_buffer_.size() < sizeof(MSG_header))
        {
            LOG_DEBUG("缓冲区数据不足以读取FILE_END消息");
            return true;
        }

        // 从缓冲区移除消息头
        read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + sizeof(MSG_header));
    }

    LOG_INFO("文件传输完成: 发送者={}, 文件名={}",
             header.sender_name, current_file_info_.filename);

    // 转发文件结束消息给其他客户端，使用正确的发送者名称
    server_->broadcastMessage(encodeFileEndMessage(header.sender_name), client_fd_);

    // 重置文件传输状态
    resetFileTransferState();

    return true;
}

void ClientHandler::resetFileTransferState()
{
    is_receiving_file_ = false;
    memset(&current_file_info_, 0, sizeof(current_file_info_));
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
            LOG_DEBUG("成功发送完整消息，大小: {} 字节", sent);
        }
        else
        {
            // 部分发送，修改队列中的消息
            auto &front_msg = const_cast<std::vector<char> &>(write_queue_.front());
            front_msg.erase(front_msg.begin(), front_msg.begin() + sent);
            LOG_DEBUG("部分发送 {} 字节，剩余 {} 字节", sent, front_msg.size());
            break;
        }
    }

    // 如果写队列为空，移除写事件
    if (write_queue_.empty())
    {
        server_->getReactor().modifyHandler(client_fd_, EventType::READ);
    }
}

void ClientHandler::handleError()
{
    LOG_INFO("客户端连接异常或断开: {} (fd: {})", client_.address, client_fd_);

    // 重置文件传输状态
    if (is_receiving_file_)
    {
        LOG_WARN("文件传输因连接断开而中断，文件名: {}", current_file_info_.filename);
        resetFileTransferState();
    }

    handleExitMessage();
}

bool ClientHandler::sendMessage(const std::vector<char> &message)
{
    // 这里做的只是将数据打包到发送队列并注册写事件,发送由hanleWrite处理
    if (client_fd_ < 0)
    {
        LOG_ERROR("尝试向已关闭的连接发送消息");
        return false;
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push(message);

    // 注册写事件
    server_->getReactor().modifyHandler(client_fd_,
                                        static_cast<EventType>(static_cast<uint32_t>(EventType::READ) |
                                                               static_cast<uint32_t>(EventType::WRITE)));
    return true;
}

bool ClientHandler::handleCompleteMessage(const MSG_header &header, const std::string &msg)
{
    LOG_DEBUG("处理消息 - 类型: {}, 发送者: {}, 长度: {}",
              getMessageTypeName(header.Type), header.sender_name, header.length);

    switch (header.Type)
    {
    case REGISTER:
    case LOGIN:
        g_authClient->processAuthRequest(client_fd_, header, msg);
        break;
    case INITIAL:
        LOG_WARN("收到未预期的INITIAL消息类型");
        break;
    case JOIN:
        return handleJoinMessage(header);
    case GROUP_MSG:
        handleGroupMessage(header, msg);
        break;
    case FILE_MSG:
    case FILE_DATA:
    case FILE_END:
        // 这些消息类型在processOneMessage中已经处理
        LOG_WARN("文件相关消息不应该在此处处理");
        break;
    case EXIT:
        handleExitMessage();
        return false;
    case TEST:
        hanleTestMessage(header, msg);
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

    // 设置客户端名称
    client_.name = username;
    LOG_INFO("客户端 {} (fd: {}) 设置名称为: {}", client_.address, client_fd_, client_.name);

    // 1. 发送在线用户列表给新用户
    server_->syncUserListForClient(getFd());

    // 2. 广播新用户加入的消息给其他所有用户
    server_->broadcastMessage(encodeMessage(JOIN, "", username), client_fd_);
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

    server_->broadcastMessage(encodeMessage(header, msg), client_fd_);
}

void ClientHandler::handleExitMessage()
{
    LOG_INFO("客户端 {} 即将退出", client_.name);

    std::string client_name = client_.name; // 备份客户端名称
    int current_fd = client_fd_;

    // 步骤1: 从服务器核心数据结构中移除此客户端
    server_->removeClient(current_fd);

    // 步骤2: 如果客户端已经登录，则广播其退出消息
    if (!client_name.empty())
    {
        server_->broadcastMessage(encodeMessage(EXIT, "", client_name), -1);
    }
}

void ClientHandler::cleanup()
{
    if (client_fd_ >= 0)
    {
        LOG_DEBUG("清理客户端连接，fd: {}", client_fd_);
        close(client_fd_);
        client_fd_ = -1;
    }

    // 清理缓冲区
    {
        std::lock_guard<std::mutex> lock(read_buffer_mutex_);
        read_buffer_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        while (!write_queue_.empty())
        {
            write_queue_.pop();
        }
    }

    // 重置文件传输状态
    resetFileTransferState();
}

void ClientHandler::hanleTestMessage(const MSG_header &header, const std::string &msg)
{
    LOG_DEBUG("处理测试消息 - 发送者: {}, 内容: {}", header.sender_name, msg);
    // 发送成功响应

    sendMessage(encodeMessage(TEST_success, "", ""));
}