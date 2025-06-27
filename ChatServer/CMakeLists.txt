cmake_minimum_required(VERSION 3.10)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)

# 查找fmt库
find_package(fmt REQUIRED)

add_executable(chatserver
    main.cpp
    server/Server.cpp
    threadpool/ThreadPool.cpp
    logger/LoggerClient.cpp
)
# 日志守护进程单独的可执行文件
add_executable(loggerd
    logger/loggerd.cpp
)

# 仅包含项目根目录，因为所有的组件子目录（server, protocol等）
# 都在这个根目录下
target_include_directories(chatserver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chatserver PRIVATE pthread fmt::fmt)
target_link_libraries(loggerd PRIVATE fmt::fmt)