#pragma once

#include <string>
#include <cstring>
#include <vector>
#include <arpa/inet.h>
#include "logger/log_macros.hpp"

#define MAX_NAMEBUFFER 64
#define MAX_FILENAME 256

/*
协议设计:
1. 客户端请求连接后发送JOIN消息
2. 服务器接收到JOIN消息后，回复INITIAL消息，包含在线用户列表,同时向其他客户端发送JOIN消息
3. 服务器接收到GROUP_MSG消息后，向所有在线用户广播该消息
4. 文件传输协议：
   - FILE_MSG: 文件传输开始，包含文件名和文件大小
   - FILE_DATA: 文件数据块，length字段表示数据块大小
   - FILE_END: 文件传输结束标志
5. 客户端发送EXIT消息时，服务器将其从在线用户列表中移除，并向其他用户广播该用户已退出
*/
enum MSG_type 
{
    REGISTER,
    REGISTER_success,
    REGISTER_failed,
    LOGIN,
    LOGIN_success,
    LOGIN_failed,

    INITIAL,
    JOIN,
    EXIT,
    GROUP_MSG,
    FILE_MSG,    // 文件传输开始，包含文件名和大小
    FILE_DATA,   // 文件数据块
    FILE_END     // 文件传输结束
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
        case FILE_DATA: return "FILE_DATA";
        case FILE_END: return "FILE_END";
        default: return "UNKNOWN";
    }
}

// 定义消息头结构
// 这里的长度字段是指消息内容的长度，不包括头部
// 头部包含发送者名称、消息类型和消息长度
// FILE_MSG: length为文件名长度，后续跟文件名和文件大小
// FILE_DATA: length为数据块大小，后续跟数据内容
// FILE_END: length为0
struct MSG_header 
{
    char sender_name[MAX_NAMEBUFFER];
    MSG_type Type;
    size_t length;
};

// 文件信息结构（用于FILE_MSG）
struct FileInfo
{
    char filename[MAX_FILENAME];
    size_t file_size;
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

inline std::vector<char> encodeMessage(const MSG_header &header, const std::string &msg) 
{
    size_t total_size = sizeof(header) + header.length;
    std::vector<char> message(total_size);
    memcpy(message.data(), &header, sizeof(header));
    memcpy(message.data() + sizeof(header), msg.c_str(), msg.length());
    
    LOG_DEBUG("[发送] 消息类型: {}, 发送者: {}, 长度: {}, 内容: {}", 
              getMessageTypeName(header.Type), header.sender_name, header.length, msg);
              
    return message;
}

// 编码文件开始消息
inline std::vector<char> encodeFileStartMessage(const std::string& sender, const std::string& filename, size_t file_size)
{
    MSG_header header;
    strncpy(header.sender_name, sender.c_str(), MAX_NAMEBUFFER - 1);
    header.sender_name[MAX_NAMEBUFFER - 1] = '\0';
    header.Type = FILE_MSG;
    header.length = sizeof(FileInfo);

    FileInfo file_info;
    strncpy(file_info.filename, filename.c_str(), MAX_FILENAME - 1);
    file_info.filename[MAX_FILENAME - 1] = '\0';
    file_info.file_size = file_size;

    std::vector<char> packet(sizeof(header) + sizeof(FileInfo));
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data() + sizeof(header), &file_info, sizeof(FileInfo));

    LOG_DEBUG("[发送] 文件开始消息 - 发送者: {}, 文件名: {}, 大小: {}", 
              sender, filename, file_size);

    return packet;
}

// 编码文件数据消息
inline std::vector<char> encodeFileDataMessage(const std::string& sender, const std::vector<char>& data)
{
    MSG_header header;
    strncpy(header.sender_name, sender.c_str(), MAX_NAMEBUFFER - 1);
    header.sender_name[MAX_NAMEBUFFER - 1] = '\0';
    header.Type = FILE_DATA;
    header.length = data.size();

    std::vector<char> packet(sizeof(header) + data.size());
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data() + sizeof(header), data.data(), data.size());

    LOG_DEBUG("[发送] 文件数据消息 - 发送者: {}, 数据大小: {}", sender, data.size());

    return packet;
}

// 编码文件结束消息
inline std::vector<char> encodeFileEndMessage(const std::string& sender)
{
    MSG_header header;
    strncpy(header.sender_name, sender.c_str(), MAX_NAMEBUFFER - 1);
    header.sender_name[MAX_NAMEBUFFER - 1] = '\0';
    header.Type = FILE_END;
    header.length = 0;

    std::vector<char> packet(sizeof(header));
    memcpy(packet.data(), &header, sizeof(header));

    LOG_DEBUG("[发送] 文件结束消息 - 发送者: {}", sender);

    return packet;
}