#pragma once

#include "threadpool/ThreadPool.hpp"
#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <thread>

// 事件类型枚举
enum class EventType
{
    READ = EPOLLIN,
    WRITE = EPOLLOUT,
    ERROR = EPOLLERR | EPOLLHUP | EPOLLRDHUP
    // 包含了 epoll 的错误、挂断和对端关闭等多种异常情况
};

// 事件处理器基类
class EventHandler
{
public:
    virtual ~EventHandler() = default;
    virtual void handleRead() = 0;
    virtual void handleWrite() = 0;
    virtual void handleError() = 0;
    virtual int getFd() const = 0;
    std::atomic_flag reading_flag_ = ATOMIC_FLAG_INIT;
};

// Reactor事件循环器
class Reactor
{
public:
    Reactor();
    ~Reactor();

    // 禁用拷贝构造和赋值
    Reactor(const Reactor &) = delete;
    Reactor &operator=(const Reactor &) = delete;

    // 启动和停止事件循环
    void run();
    void stop();
    bool isRunning() const { return running_; }

    // 注册移除修改事件处理器
    bool registerHandler(std::shared_ptr<EventHandler> handler, EventType events);
    bool removeHandler(int fd);
    bool modifyHandler(int fd, EventType events);

    // 设置线程池
    void setThreadPool(std::shared_ptr<ThreadPool> pool);

    // 投递任务到线程池
    template <typename F>
    void postTask(F &&task);

private:
    static const int MAX_EVENTS = 1024;
    static const int EPOLL_TIMEOUT = -1; // 无限等待
    static const int EPOLL_TIMEOUT_MS = 1000; // 1秒超时,检查运行标志

    int epoll_fd_;
    std::atomic<bool> running_;
    std::unordered_map<int, std::shared_ptr<EventHandler>> handlers_;
    std::shared_ptr<ThreadPool> thread_pool_;

    void handleEvents();
    void processEvent(const epoll_event &event);
};

// 模板方法实现
template <typename F>
void Reactor::postTask(F &&task)// postTask只接受一个参数F，表示一个可调用对象（函数、lambda等）
{
    if (thread_pool_)
    {
        thread_pool_->enqueue(std::forward<F>(task));
    }
    else
    {
        // 如果没有线程池，直接执行
        task();
    }
}