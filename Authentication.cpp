#include "Authentication.hpp"
#include <cstring>
#include <string>
#include <iostream>

std::shared_ptr<AuthClient> g_authClient = nullptr;

void AuthClient::processAuthRequest(int client_fd, const MSG_header &header, const std::string &payload)
{
    std::string username(header.sender_name);

    bool rpcSuccess = false;
    std::string rpcMessage;

    // payload 里只包含 encrypted_hash
    // 直接传递给 RPC

    if (header.Type == LOGIN)
    {
        rpcSuccess = Login(username, payload, rpcMessage);
    }
    else if (header.Type == REGISTER)
    {
        rpcSuccess = Register(username, payload, rpcMessage);
    }
    else
    {
        rpcSuccess = false;
        rpcMessage = "未知认证类型";
    }

    // 构造响应包
    MSG_header respHeader{};
    memset(respHeader.sender_name, 0, sizeof(respHeader.sender_name));
    respHeader.Type = (header.Type == LOGIN) ? (rpcSuccess ? LOGIN_success : LOGIN_failed)
                                             : (rpcSuccess ? REGISTER_success : REGISTER_failed);
    respHeader.length = 0;

    LOG_INFO("处理认证请求: 类型={}, 用户名={}, 成功={}, Auth返回消息={}",
             (header.Type == LOGIN) ? "LOGIN" : "REGISTER", username, rpcSuccess, rpcMessage);

    std::string resp(reinterpret_cast<char *>(&respHeader), sizeof(respHeader));

    // std::cout << "Header size: " << sizeof(MSG_header) << std::endl;
    // std::cout << "Response message: " << resp.size() << std::endl;

    // 发送响应
    send(client_fd, resp.data(), resp.size(), 0);
}
