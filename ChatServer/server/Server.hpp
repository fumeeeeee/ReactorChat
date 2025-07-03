#pragma once

#include "../protocol/Protocol.hpp"
#include <map>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <mutex>
#include <sys/epoll.h>
#include <chrono>
#include <unordered_map>
#include <thread>
#include <atomic>

#define MAX_NAMEBUFFER 64
// 定义客户端信息结构
struct Client 
{
    std::string address;
    std::string Name;
    bool isRecving = false;
};

class Server 
{
public:
    // 删除拷贝构造函数和赋值运算符
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // 获取单例实例的静态方法
    static Server& getInstance(int port = 1234);

    void start();
    void stop();

private:
    // 私有构造函数
    Server(int port);
    ~Server();

    int port;
    int server_fd;
    int epoll_fd;
    std::atomic<bool> running;
    std::unordered_map<int, Client> clients;

    void handleNewConnection();
    bool handleClientMessage(int client_fd);
    bool handleInitialMessage(int client_fd, const std::string& msg);
    bool handleJoinMessage(int client_fd, const MSG_header& header);  
    void handleGroupMessage(int client_fd, const MSG_header& header, const std::string& msg);  
    void sendUserListToClient(int client_fd); 
    void handleExitMessage(int client_fd); 
    void handleClientDisconnect(int client_fd); 
    void broadcast(const std::vector<char>& message); 
    void syncUserList(); 
    void prepareFileTransfer(int client_fd, const MSG_header& header); 
    void safeBroadcast(const std::vector<char>& message);
};