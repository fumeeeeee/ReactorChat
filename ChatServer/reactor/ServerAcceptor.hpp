#pragma once

#include <logger/log_macros.hpp>
#include "Reactor.hpp"
#include "ReactorServer.hpp"
#include <fcntl.h>
#include <unistd.h>
#include "ClientHandler.hpp"

class ReactorServer;

// 服务器监听器 - 只处理新连接
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