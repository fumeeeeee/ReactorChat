#pragma once

#include "protocol/Protocol.hpp"
#include <grpcpp/grpcpp.h>
#include "logger/log_macros.hpp"
#include "rpcGenerated/Auth.grpc.pb.h"
#include "rpcGenerated/Auth.pb.h"

#include <string>
#include <memory>

// RPC客户端视角:
// protobuf提供了stub(代理),stub可以使用注册的RPC方法(参数是context,request,response),返回status
// request和response是protobuf定义的message,包含了约定的数据结构

class AuthClient
{
public:
    explicit AuthClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(auth::AuthService::NewStub(channel)) {}

    // 处理客户端请求接口
    void processAuthRequest(int client_fd, const MSG_header &header, const std::string &payload);

private:
    std::unique_ptr<auth::AuthService::Stub> stub_;

    // 内联定义
    bool Login(const std::string &username,
               const std::string &encryptedHash,
               std::string &outMessage)
    {
        auth::AuthRequest request;
        request.set_username(username);
        request.set_encrypted_hash(encryptedHash);

        auth::AuthResponse response;
        grpc::ClientContext context;

        grpc::Status status = stub_->Login(&context, request, &response);

        if (!status.ok())
        {
            outMessage = "RPC调用失败: " + status.error_message();
            return false;
        }

        outMessage = response.message();
        return response.success();
    }

    bool Register(const std::string &username,
                  const std::string &encryptedHash,
                  std::string &outMessage)
    {
        auth::AuthRequest request;
        request.set_username(username);
        request.set_encrypted_hash(encryptedHash);

        auth::AuthResponse response;
        grpc::ClientContext context;

        grpc::Status status = stub_->Register(&context, request, &response);

        if (!status.ok())
        {
            outMessage = "RPC调用失败: " + status.error_message();
            return false;
        }

        outMessage = response.message();
        return response.success();
    }
};