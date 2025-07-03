#pragma once

#include "Reactor.hpp"
#include "ReactorServer.hpp"
#include "../protocol/Protocol.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <queue>

class ReactorServer;

class ClientHandler : public EventHandler
{
public:
    ClientHandler(int client_fd, const std::string &address, ReactorServer *server);
    ~ClientHandler() override;

    void handleRead() override;
    void handleWrite() override;
    void handleError() override;

    int getFd() const { return client_fd_; }
    const std::string &getName() const { return client_.name; }
    bool isNameSet() const { return !client_.name.empty(); }
    bool sendMessage(const std::vector<char> &message);

private:
    struct ClientInfo
    {
        int fd;
        std::string address;
        std::string name;
        bool isReceiving;
    };

    void cleanup();
    bool handleCompleteMessage(const MSG_header &header, const std::string &msg);
    bool handleJoinMessage(const MSG_header &header);
    void handleGroupMessage(const MSG_header &header, const std::string &msg);
    void handleFileMessage(const MSG_header &header);
    void handleExitMessage();
    bool processMessages();
    bool processFileTransfer();

    int client_fd_;
    ReactorServer *server_;
    ClientInfo client_;
    std::vector<char> read_buffer_;
    std::queue<std::vector<char>> write_queue_;
    std::mutex write_mutex_;
    std::mutex read_buffer_mutex_;
};