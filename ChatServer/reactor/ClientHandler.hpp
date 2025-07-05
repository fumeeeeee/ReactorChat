#pragma once

#include "authClient/Authentication.hpp"
#include "Reactor.hpp"
#include "ReactorServer.hpp"
#include "protocol/Protocol.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <sys/ioctl.h> // for ioctl, FIONREAD

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
    };

    void cleanup();
    bool handleCompleteMessage(const MSG_header &header, const std::string &msg);
    bool handleJoinMessage(const MSG_header &header);
    void handleGroupMessage(const MSG_header &header, const std::string &msg);
    void handleExitMessage();
    void hanleTestMessage(const MSG_header &header, const std::string &msg);

    // 消息处理相关
    void processMessages();
    bool processOneMessage();
    bool handleRegularMessage(const MSG_header &header);

    // 文件传输相关方法
    bool handleFileStartMessage(const MSG_header &header);
    bool handleFileDataMessage(const MSG_header &header);
    bool handleFileEndMessage(const MSG_header &header);
    void resetFileTransferState();

    int client_fd_;
    ReactorServer *server_;
    ClientInfo client_;

    // 读写缓冲区和队列
    std::vector<char> read_buffer_;
    std::queue<std::vector<char>> write_queue_;
    std::mutex write_mutex_;
    std::mutex read_buffer_mutex_;

    // 文件传输状态
    bool is_receiving_file_;        // 是否正在接收文件
    FileInfo current_file_info_;    // 当前文件信息
    size_t received_file_bytes_;    // 已接收的文件字节数
    std::mutex file_receive_mutex_; // 文件接收互斥锁
};