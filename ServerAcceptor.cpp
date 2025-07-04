#include "ServerAcceptor.hpp"


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
                break; // 没有更多连接,跳出
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
    LOG_ERROR("ServerAcceptor收到写事件，这不应该发生");
}

void ServerAcceptor::handleError()
{
    LOG_ERROR("ServerAcceptor发生错误，fd: {}", listen_fd_);
}
