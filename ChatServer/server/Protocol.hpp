#pragma once

#include <string>
#include <cstring>
#include <vector>
#include <arpa/inet.h>
#include "logger/log_macros.hpp"

#define MAX_NAMEBUFFER 64
// 定义消息类型枚举
enum MSG_type 
{
    INITIAL,
    JOIN,
    EXIT,
    GROUP_MSG,
    FILE_MSG,
    FAILED
};

// enum_to_string
inline const char* getMessageTypeName(MSG_type type) 
{
    switch (type) 
    {
        case INITIAL: return "INITIAL";
        case JOIN: return "JOIN";
        case EXIT: return "EXIT";
        case GROUP_MSG: return "GROUP_MSG";
        case FILE_MSG: return "FILE_MSG";
        case FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

// 定义消息头结构
struct MSG_header 
{
    char sender_name[MAX_NAMEBUFFER];
    MSG_type Type;
    size_t length;
};

// 消息编码函数
inline std::vector<char> encodeMessage(MSG_type type, const std::string& msg, const std::string& sender = "Server") 
{
    MSG_header header;
    strncpy(header.sender_name, sender.c_str(), MAX_NAMEBUFFER - 1);
    header.sender_name[MAX_NAMEBUFFER - 1] = '\0';
    header.Type = type;
    header.length = msg.length();

    std::vector<char> packet(sizeof(header) + msg.length());
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data() + sizeof(header), msg.c_str(), msg.length());
    
    LOG_DEBUG("[发送] 消息类型: {}, 发送者: {}, 长度: {}, 内容: {}", 
              getMessageTypeName(type), sender, msg.length(), msg);
    
    return packet;
}

// 消息解码函数
inline std::pair<MSG_header, std::string> decodeMessage(const char* data, size_t length) 
{
    if (length < sizeof(MSG_header)) 
    {
        LOG_ERROR("消息解码失败: 数据长度 {} 小于消息头大小 {}", length, sizeof(MSG_header));
        return {{}, ""};
    }

    MSG_header header;
    memcpy(&header, data, sizeof(header));
    
    std::string msg(data + sizeof(header), length - sizeof(header));
    
    LOG_DEBUG("[接收] 消息类型: {}, 发送者: {}, 长度: {}, 内容: {}", 
              getMessageTypeName(header.Type), header.sender_name, header.length, msg);
    
    return {header, msg};
}
