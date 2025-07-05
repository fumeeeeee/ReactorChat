cmake_minimum_required(VERSION 3.10)

project(ChatServer)

# 使用C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 如果有自定义的依赖路径，可以放这里
set(CMAKE_PREFIX_PATH "$ENV{HOME}/.local")

# 编译选项：针对 GNU 或 Clang 编译器
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # 警告和错误
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

    # 调试和安全检查选项
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_compile_options(-fno-omit-frame-pointer -g -O1)

    # 链接选项（sanitizer 需要链接）
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()


# 查找fmt库（格式化输出）
find_package(fmt REQUIRED)

# 查找Protobuf（协议缓冲区）
find_package(Protobuf REQUIRED)

# 查找gRPC库
find_package(gRPC REQUIRED)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${GRPC_INCLUDE_DIRS}
)

# 通过包管理器安装的mysqlcppconn库一般默认在系统路径下，直接链接即可
# 这里我们不需要find_library，直接在target_link_libraries里写名字即可

# 添加chatserver可执行文件
add_executable(chatserver
    main.cpp
    reactor/Reactor.cpp
    reactor/ClientHandler.cpp
    reactor/ReactorServer.cpp
    reactor/ServerAcceptor.cpp
    threadpool/ThreadPool.cpp
    logger/LoggerClient.cpp

    authClient/Authentication.cpp

    # gRPC自动生成的代码
    rpcGenerated/Auth.pb.cc
    rpcGenerated/Auth.grpc.pb.cc
)

# 添加loggerd可执行文件（日志守护进程）
add_executable(loggerd
    logger/loggerd.cpp
)

# 添加authserver可执行文件（认证服务）
add_executable(authserver
    authServer/AuthServer.cpp
    authServer/AuthServiceImpl.cpp

    # gRPC自动生成的代码
    rpcGenerated/Auth.pb.cc
    rpcGenerated/Auth.grpc.pb.cc
)

# 目标链接依赖

# chatserver 链接依赖
target_link_libraries(chatserver
    PRIVATE
        pthread              # 线程库
        fmt::fmt             # fmt格式库
        gRPC::grpc++         # gRPC客户端库
        protobuf::libprotobuf # protobuf库
)

# loggerd 链接依赖
target_link_libraries(loggerd
    PRIVATE
        fmt::fmt
)

# authserver 链接依赖，注意加入mysqlcppconn
target_link_libraries(authserver
    PRIVATE
        pthread
        fmt::fmt
        gRPC::grpc++
        protobuf::libprotobuf
        mysqlcppconn       # MySQL Connector/C++ 库
)
