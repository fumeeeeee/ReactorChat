#include "rpcGenerated/Auth.grpc.pb.h"
#include "rpcGenerated/Auth.pb.h"
#include <grpcpp/grpcpp.h>

#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <iostream>
#include <memory>
#include <mutex>

using grpc::Server;
using grpc::ServerContext;
using grpc::Status;

using namespace auth;

// 服务器视角:
// protobuf 为服务器生成一个抽象的 Service 类，里面定义了虚函数（对应每个 RPC 方法）。
// 需要继承这个Service 类，并实现每个虚函数，处理传来的 request，填充 response，返回 Status

// MySQLConnector/C++:
// 1. 获取driver实例
// 2. 使用driver连接数据库得到conn对象
// 3. 使用conn选择数据库
// 4. 使用conn创建PreparedStatement/Statement对象
// 5. 使用PreparedStatement/Statement执行SQL语句
// 6. 使用ResultSet处理查询结果
// 7. 最后关闭连接

class AuthServiceImpl final : public AuthService::Service
{
public:
    AuthServiceImpl(const std::string &db_uri,
                    const std::string &user,
                    const std::string &password,
                    const std::string &db_name)
        : db_uri_(db_uri), user_(user), password_(password), db_name_(db_name)
    {
        initializeConnection();
    }

    ~AuthServiceImpl()
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        if (conn && !conn->isClosed())
        {
            try
            {
                conn->close();
            }
            catch (const std::exception &e)
            {
                std::cerr << "关闭数据库连接时发生错误: " << e.what() << std::endl;
            }
        }
        conn.reset();
    }

    // 禁用拷贝构造和拷贝赋值
    AuthServiceImpl(const AuthServiceImpl &) = delete;
    AuthServiceImpl &operator=(const AuthServiceImpl &) = delete;

    Status Register([[maybe_unused]] ServerContext *context, const auth::AuthRequest *request, auth::AuthResponse *response) override
    {
        std::lock_guard<std::mutex> lock(db_mutex);

        // 确保连接有效
        if (!ensureConnection())
        {
            response->set_success(false);
            response->set_message("数据库连接失败");
            return Status::OK;
        }

        try
        {
            // SELECT查询
            std::unique_ptr<sql::PreparedStatement> check(
                conn->prepareStatement("SELECT username FROM users WHERE username = ?"));
            check->setString(1, request->username());

            std::unique_ptr<sql::ResultSet> res(check->executeQuery());
            if (res->next())
            {
                response->set_success(false);
                response->set_message("用户名已存在");
                return Status::OK;
            }

            std::unique_ptr<sql::PreparedStatement> insert(
                conn->prepareStatement("INSERT INTO users(username, password_hash) VALUES(?, ?)"));
            insert->setString(1, request->username());
            insert->setString(2, request->encrypted_hash());
            insert->execute();

            response->set_success(true);
            response->set_message("注册成功");
        }
        catch (const sql::SQLException &e)
        {
            std::cerr << "数据库操作错误: " << e.what() << std::endl;
            response->set_success(false);
            response->set_message("数据库操作失败");
        }
        catch (const std::exception &e)
        {
            std::cerr << "注册操作错误: " << e.what() << std::endl;
            response->set_success(false);
            response->set_message("注册失败");
        }

        return Status::OK;
    }

    Status Login([[maybe_unused]] ServerContext *context, const auth::AuthRequest *request, auth::AuthResponse *response) override
    {
        std::lock_guard<std::mutex> lock(db_mutex);

        // 确保连接有效
        if (!ensureConnection())
        {
            response->set_success(false);
            response->set_message("数据库连接失败");
            return Status::OK;
        }

        try
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("SELECT password_hash FROM users WHERE username = ?"));
            stmt->setString(1, request->username());

            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
            if (!res->next())
            {
                response->set_success(false);
                response->set_message("用户不存在");
                return Status::OK;
            }

            std::string stored_RSA = res->getString("password_hash");
            std::string client_RSA = request->encrypted_hash();

            auto stored_hash = rsaDecrypt(stored_RSA, "/root/ChatServerOPEN/authServer/private.pem");
            auto client_hash = rsaDecrypt(client_RSA, "/root/ChatServerOPEN/authServer/private.pem");

            if (stored_hash.empty() || client_hash.empty() || stored_hash != client_hash)
            {
                response->set_success(false);
                response->set_message("密码错误");
            }
            else
            {
                response->set_success(true);
                response->set_message("登录成功");
            }
        }
        catch (const sql::SQLException &e)
        {
            std::cerr << "数据库操作错误: " << e.what() << std::endl;
            response->set_success(false);
            response->set_message("数据库操作失败");
        }
        catch (const std::exception &e)
        {
            std::cerr << "登录操作错误: " << e.what() << std::endl;
            response->set_success(false);
            response->set_message("登录失败");
        }

        return Status::OK;
    }

private:
    void initializeConnection()
    {
        try
        {
            driver = sql::mysql::get_mysql_driver_instance();
            conn = std::unique_ptr<sql::Connection>(driver->connect(db_uri_, user_, password_));
            conn->setSchema(db_name_);
        }
        catch (const sql::SQLException &e)
        {
            std::cerr << "初始化数据库连接失败: " << e.what() << std::endl;
            conn.reset();
        }
    }

    bool ensureConnection()
    {
        try
        {
            if (!conn || conn->isClosed())
            {
                initializeConnection();
            }
            return conn && !conn->isClosed();
        }
        catch (const std::exception &e)
        {
            std::cerr << "检查数据库连接时发生错误: " << e.what() << std::endl;
            return false;
        }
    }

    std::vector<uint8_t> rsaDecrypt(const std::string &encryptedData, const std::string &privateKeyPath);

    // 数据库连接参数
    std::string db_uri_;
    std::string user_;
    std::string password_;
    std::string db_name_;

    sql::mysql::MySQL_Driver *driver;
    std::unique_ptr<sql::Connection> conn;
    std::mutex db_mutex; // 多线程安全
};