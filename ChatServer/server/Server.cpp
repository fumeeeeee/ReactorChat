#include "Server.hpp"
#include "protocol/Protocol.hpp"
#include "threadpool/ThreadPool.hpp"
#include "logger/LoggerClient.hpp"
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <algorithm>
#include <sys/socket.h>
#include <errno.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <sys/epoll.h>
#include <csignal>

#define MAX_EVENTS 64

// 声明外部变量
extern Server *g_server;

// 静态成员变量初始化
Server &Server::getInstance(int port)
{
    static Server instance(port);
    return instance;
}

Server::Server(int port) : port(port), server_fd(-1), epoll_fd(-1), running(false)
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        LOG_ERROR("无法创建套接字: {}", strerror(errno));
        throw std::runtime_error("无法创建套接字");
    }

    // 设置套接字选项
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        LOG_ERROR("无法设置套接字选项: {}", strerror(errno));
        throw std::runtime_error("无法设置套接字选项");
    }

    // 设置非阻塞模式
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    // 绑定地址
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        LOG_ERROR("无法绑定端口 {}: {}", port, strerror(errno));
        throw std::runtime_error("无法绑定端口");
    }

    // 监听连接
    if (listen(server_fd, SOMAXCONN) < 0)
    {
        LOG_ERROR("无法监听连接: {}", strerror(errno));
        throw std::runtime_error("无法监听连接");
    }

    // 创建epoll实例
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        LOG_ERROR("无法创建epoll实例: {}", strerror(errno));
        throw std::runtime_error("无法创建epoll实例");
    }

    // 添加服务器套接字到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0)
    {
        LOG_ERROR("无法添加服务器套接字到epoll: {}", strerror(errno));
        throw std::runtime_error("无法添加服务器套接字到epoll");
    }

    LOG_INFO("服务器初始化完成，监听端口: {}", port);
}

Server::~Server()
{
    stop();
    if (server_fd >= 0)
        close(server_fd);
    if (epoll_fd >= 0)
        close(epoll_fd);
    LOG_INFO("服务器已关闭");
}

void Server::start()
{
    running = true;
    LOG_INFO("服务器启动");

    struct epoll_event events[MAX_EVENTS];
    while (running)
    {
        LOG_DEBUG("循环入口");
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR)
                continue;
            LOG_ERROR("epoll_wait失败: {}", strerror(errno));
            break;
        }

        // 收集需要断开的客户端
        std::vector<int> clients_to_disconnect;

        for (int i = 0; i < nfds; ++i)
        {
            int fd = events[i].data.fd;
            int ev = events[i].events;
            try
            {
                if (fd == server_fd)
                {
                    handleNewConnection();
                }
                else
                {
                    if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
                    {
                        LOG_INFO("检测到客户端断开或异常 (fd: {}, event: {})", fd, ev);
                        clients_to_disconnect.push_back(fd);
                        continue;
                    }

                    if (!handleClientMessage(fd))
                    {
                        clients_to_disconnect.push_back(fd);
                    }
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("处理事件时发生错误: {}", e.what());
                if (fd != server_fd)
                {
                    clients_to_disconnect.push_back(fd);
                }
            }
        }

        // 批量处理需要断开的连接
        for (int client_fd : clients_to_disconnect)
        {
            handleClientDisconnect(client_fd);
        }
    }
}

void Server::stop()
{
    LOG_INFO("开始停止服务器...");
    running = false;
    for (const auto &client : clients)
    {
        close(client.first);
    }
    clients.clear();
    LOG_INFO("服务器已停止，所有客户端连接已关闭");
}

void Server::handleNewConnection()
{
    while (true)
    { // 边缘触发模式下需要循环接受所有连接
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // 没有更多连接了
            }
            LOG_ERROR("接受连接失败: {}", strerror(errno));
            return;
        }

        // 设置非阻塞模式
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        // 添加到epoll
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // 添加 EPOLLRDHUP
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0)
        {
            LOG_ERROR("无法添加客户端到epoll: {}", strerror(errno));
            close(client_fd);
            return;
        }

        // 创建临时客户端信息
        Client client;
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, INET_ADDRSTRLEN);
        client.address = std::string(addr_str) + ":" + std::to_string(ntohs(client_addr.sin_port));
        client.isRecving = false;
        client.Name = ""; // 初始化为空，等待INITIAL消息

        {
            clients[client_fd] = client;
        }
        LOG_INFO("新客户端连接: {} (fd: {})", client.address, client_fd);
    }
}

// 状态结构体用于追踪文件接收进度
struct FileTransferSession
{
    MSG_header header;
    size_t bytesReceived = 0;
    std::vector<char> buffer;
};

static std::unordered_map<int, FileTransferSession> fileSessions;

bool Server::handleClientMessage(int client_fd)
{
    if (clients.find(client_fd) == clients.end())
    {
        return false;
    }

    while (true)
    {
        bool didWork = false;

        // 文件传输中优先处理 
        if (fileSessions.count(client_fd))
        {
            auto &session = fileSessions[client_fd];
            const size_t CHUNK = 4096;
            char tempBuf[CHUNK];

            while (session.bytesReceived < session.header.length)
            {
                ssize_t r = recv(client_fd, tempBuf, std::min(CHUNK, session.header.length - session.bytesReceived), 0);
                if (r <= 0)
                {
                    if (r == 0)
                    {
                        LOG_INFO("客户端中途断开连接 (文件接收中): fd = {}", client_fd);
                        return false;
                    }
                    else if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        return true;
                    }
                    else
                    {
                        LOG_WARN("客户端异常断开 (文件接收中): fd = {}, errno = {}", client_fd, errno);
                        return false;
                    }
                }

                session.buffer.insert(session.buffer.end(), tempBuf, tempBuf + r);
                session.bytesReceived += r;
                didWork = true;

                std::vector<int> failed_clients;
                {
                    for (const auto &other : clients)
                    {
                        if (other.first != client_fd && !other.second.Name.empty())
                        {
                            if (send(other.first, tempBuf, r, 0) < 0)
                            {
                                LOG_ERROR("发送文件数据失败: {}", strerror(errno));
                                failed_clients.push_back(other.first);
                            }
                        }
                    }
                }

                for (int failed_fd : failed_clients)
                {
                    handleClientDisconnect(failed_fd);
                }
            }

            if (session.bytesReceived >= session.header.length)
            {
                LOG_INFO("文件转发完成，共转发 {} 字节", session.bytesReceived);
                fileSessions.erase(client_fd);
            }

            continue; // 文件数据读完了，再尝试处理普通消息
        }

        // 查是否有完整的消息头
        MSG_header header;
        ssize_t peeked = recv(client_fd, &header, sizeof(header), MSG_PEEK);
        if (peeked == 0)
        {
            LOG_INFO("客户端主动断开连接: fd = {}", client_fd);
            return false;
        }
        else if (peeked < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
            LOG_WARN("客户端异常断开: fd = {}, errno = {}", client_fd, errno);
            return false;
        }
        else if (peeked < static_cast<ssize_t>(sizeof(header)))
        {
            return true; // 等待下次完整消息头
        }

        // 真正读取消息头
        ssize_t readHead = recv(client_fd, &header, sizeof(header), 0);
        if (readHead != sizeof(header))
        {
            return false;
        }

        std::string msg;
        if (header.length > 0 && header.Type != FILE_MSG)
        {
            std::vector<char> buffer(header.length);
            size_t received = 0;

            while (received < header.length)
            {
                ssize_t r = recv(client_fd, buffer.data() + received, header.length - received, 0);
                if (r <= 0)
                {
                    if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                    {
                        return false;
                    }
                    return true;
                }
                received += r;
            }

            if (received == header.length)
            {
                msg.assign(buffer.begin(), buffer.end());
            }
            else
            {
                return true;
            }
        }
        LOG_DEBUG("收到完整消息 - 类型: {}, 发送者: {}, 长度: {}, 内容: {}",
                  static_cast<int>(header.Type), header.sender_name, header.length, msg.c_str());

        switch (header.Type)
        {
        case INITIAL:
            if (!handleInitialMessage(client_fd, msg))
                return false;
            break;
        case JOIN:
            if (!handleJoinMessage(client_fd, header))
                return false;
            break;
        case EXIT:
            LOG_INFO("客户端请求退出: {}", header.sender_name);
            return false;
        case GROUP_MSG:
            handleGroupMessage(client_fd, header, msg);
            break;
        case FILE_MSG:
            prepareFileTransfer(client_fd, header);
            break;
        default:
            LOG_WARN("未知消息类型: {}", static_cast<int>(header.Type));
            break;
        }

        didWork = true;

        if (!didWork)
            break; // 什么也没做，退出
    }

    return true;
}

void Server::handleGroupMessage(int client_fd, const MSG_header &header, const std::string &msg)
{
    std::vector<int> failed_clients;

    auto it = clients.find(client_fd);
    if (it == clients.end())
        return;

    auto &client = it->second;
    if (client.Name.empty())
    {
        LOG_WARN("未初始化的客户端尝试发送消息");
        return;
    }

    // 转发消息给其他客户端（按原始格式）
    size_t total_size = sizeof(header) + header.length;
    std::vector<char> send_buffer(total_size);
    memcpy(send_buffer.data(), &header, sizeof(header));
    memcpy(send_buffer.data() + sizeof(header), msg.c_str(), msg.length());

    for (const auto &otherClient : clients)
    {
        if (otherClient.first != client_fd && !otherClient.second.Name.empty())
        {
            if (write(otherClient.first, send_buffer.data(), send_buffer.size()) < 0)
            {
                LOG_ERROR("转发消息失败 to fd {}: {}", otherClient.first, strerror(errno));
                failed_clients.push_back(otherClient.first);
            }
        }
    }

    LOG_INFO("转发群消息: {} -> {}", client.Name, msg);

    // 处理失败的客户端
    for (int failed_fd : failed_clients)
    {
        handleClientDisconnect(failed_fd);
    }
}

void Server::prepareFileTransfer(int client_fd, const MSG_header &header)
{
    auto it = clients.find(client_fd);
    if (it == clients.end())
        return;

    auto &client = it->second;
    if (client.Name.empty())
    {
        LOG_WARN("未初始化客户端尝试发送文件");
        return;
    }

    LOG_INFO("开始处理文件传输: 发送者={}, 文件大小={}", client.Name.c_str(), header.length);

    for (const auto &other : clients)
    {
        if (other.first != client_fd && !other.second.Name.empty())
        {
            send(other.first, &header, sizeof(header), 0);
        }
    }

    FileTransferSession session;
    session.header = header;
    session.bytesReceived = 0;
    session.buffer.reserve(header.length);
    fileSessions[client_fd] = std::move(session);
}

void Server::handleClientDisconnect(int client_fd)
{
    std::string name;
    bool client_existed = false;

    auto it = clients.find(client_fd);
    if (it == clients.end())
    {
        LOG_DEBUG("客户端 fd {} 已被处理过或不存在", client_fd);
        return; // 客户端已经被处理过了
    }

    name = it->second.Name;
    client_existed = true;

    // 从clients映射中移除
    clients.erase(it);

    // 清理文件传输会话
    fileSessions.erase(client_fd);

    // 从epoll中移除
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr) < 0)
    {
        LOG_DEBUG("从epoll中移除客户端 fd {}: {}", client_fd, strerror(errno));
    }

    // 关闭socket
    if (close(client_fd) < 0)
    {
        LOG_DEBUG("关闭客户端socket fd {}: {}", client_fd, strerror(errno));
    }

    if (client_existed && !name.empty())
    {
        LOG_INFO("客户端断开连接: {} (fd: {})", name, client_fd);

        // 广播退出消息
        MSG_header header;
        header.Type = EXIT;
        header.length = 0;
        std::strncpy(header.sender_name, name.c_str(), sizeof(header.sender_name));

        // 使用更安全的广播方式
        std::vector<char> exit_message(sizeof(header));
        memcpy(exit_message.data(), &header, sizeof(header));
        safeBroadcast(exit_message);

    }
    else if (client_existed)
    {
        LOG_INFO("未初始化的客户端断开连接 (fd: {})", client_fd);
    }
}

void Server::syncUserList()
{
    std::string userList;
    for (const auto &client : clients)
    {
        if (!client.second.Name.empty())
        {
            if (!userList.empty())
                userList += ",";
            userList += client.second.Name;
        }
    }
    safeBroadcast(encodeMessage(JOIN, "用户列表: " + userList, "Server"));
}

void Server::safeBroadcast(const std::vector<char> &message)
{
    std::vector<int> valid_clients;

    // 获取当前有效的客户端列表
    {
        for (const auto &client : clients)
        {
            if (!client.second.Name.empty())
            {
                valid_clients.push_back(client.first);
            }
        }
    } // 锁在这里自动释放

    // 发送消息并收集失败的客户端
    std::vector<int> failed_clients;
    for (int client_fd : valid_clients)
    {
        ssize_t ret = send(client_fd, message.data(), message.size(), MSG_NOSIGNAL);
        if (ret < 0 || static_cast<size_t>(ret) != message.size())
        {
            LOG_ERROR("发送消息失败 to fd {}: {}", client_fd, strerror(errno));
            failed_clients.push_back(client_fd);
        }
    }
    // 处理发送失败的客户端（可能会递归调用)
    for (int failed_fd : failed_clients)
    {
        handleClientDisconnect(failed_fd);
    }
}

void Server::broadcast(const std::vector<char> &message)
{
    safeBroadcast(message);
}

bool Server::handleInitialMessage(int client_fd, const std::string &msg)
{
    auto it = clients.find(client_fd);
    if (it == clients.end())
        return false;

    // 检查用户名是否已存在
    for (const auto &client : clients)
    {
        if (client.second.Name == msg)
        {
            // 用户名已存在，发送错误消息
            auto response = encodeMessage(FAILED, "用户名已存在", "Server");
            write(client_fd, response.data(), response.size());
            return false; // 断开客户端
        }
    }

    auto &client = it->second;
    client.Name = msg;
    LOG_INFO("客户端 {} 设置名称为: {}", client.address, client.Name);

    // 发送确认消息
    auto response = encodeMessage(INITIAL, "连接成功", "Server");
    if (write(client_fd, response.data(), response.size()) < 0)
    {
        LOG_ERROR("发送INITIAL确认失败: {}", strerror(errno));
        return false;
    }

    // 广播新用户加入
    broadcast(encodeMessage(JOIN, client.Name + " 加入了聊天室", "Server"));
    syncUserList();

    return true;
}

bool Server::handleJoinMessage(int client_fd, const MSG_header &header)
{
    auto it = clients.find(client_fd);
    if (it == clients.end())
        return false;

    std::string username = header.sender_name;

    // 检查用户名是否已存在
    for (const auto &client : clients)
    {
        if (client.first != client_fd && client.second.Name == username)
        {
            // 用户名已存在，发送错误消息
            auto response = encodeMessage(FAILED, "用户名已存在", "Server");
            write(client_fd, response.data(), response.size());
            return false; // 断开客户端
        }
    }

    auto &client = it->second;
    client.Name = username;
    LOG_INFO("客户端 {} 设置名称为: {}", client.address, client.Name);

    // 发送在线用户列表给新用户
    sendUserListToClient(client_fd);

    // 广播新用户加入给其他用户
    MSG_header joinHeader;
    strcpy(joinHeader.sender_name, username.c_str());
    joinHeader.Type = JOIN;
    joinHeader.length = 0;

    for (const auto &otherClient : clients)
    {
        if (otherClient.first != client_fd && !otherClient.second.Name.empty())
        {
            write(otherClient.first, &joinHeader, sizeof(joinHeader));
        }
    }

    return true;
}

void Server::sendUserListToClient(int client_fd)
{
    std::ostringstream oss;
    bool first = true;

    for (const auto &user : clients)
    {
        if (user.first != client_fd && !user.second.Name.empty())
        {
            if (!first)
                oss << ",";
            oss << user.second.Name;
            first = false;
        }
    }

    std::string userList = oss.str();
    if (userList.empty())
        return;

    MSG_header header;
    strcpy(header.sender_name, "SERVER");
    header.Type = INITIAL;
    header.length = userList.length();

    size_t total_size = sizeof(header) + header.length;
    std::vector<char> messageBuffer(total_size);
    memcpy(messageBuffer.data(), &header, sizeof(header));
    memcpy(messageBuffer.data() + sizeof(header), userList.c_str(), userList.length());

    write(client_fd, messageBuffer.data(), messageBuffer.size());
    LOG_DEBUG("发送用户列表给客户端: {}", userList);
}

void Server::handleExitMessage(int client_fd)
{
    handleClientDisconnect(client_fd);
}