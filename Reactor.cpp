#include "Reactor.hpp"
#include "threadpool/ThreadPool.hpp"
#include "logger/log_macros.hpp"
#include <unistd.h>
#include <cstring>
#include <stdexcept>

Reactor::Reactor() : epoll_fd_(-1), running_(false)
{
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    // EPOLL_CLOEXEC 的作用是为新创建的文件描述符设置 FD_CLOEXEC 标志
    // 进程执行exec系列函数时，所有带有FD_CLOEXEC标志的文件描述符都会被自动关闭
    // 防止文件描述符意外泄漏到子进程
    if (epoll_fd_ < 0)
    {
        LOG_ERROR("创建epoll失败: {}", strerror(errno));
        throw std::runtime_error("创建epoll失败");
    }
    LOG_DEBUG("Reactor初始化成功，epoll_fd: {}", epoll_fd_);
}

Reactor::~Reactor()
{
    stop();
    if (epoll_fd_ >= 0)
    {
        close(epoll_fd_);
    }
    LOG_DEBUG("Reactor析构完成");
}

void Reactor::run()
{
    running_ = true;
    LOG_INFO("Reactor开始运行");

    while (running_)
    {
        handleEvents();
    }

    LOG_INFO("Reactor停止运行");
}

void Reactor::stop()
{
    running_ = false;
    LOG_INFO("Reactor准备停止");
}

bool Reactor::registerHandler(std::shared_ptr<EventHandler> handler, EventType events)
{
    if (!handler)
    {
        LOG_ERROR("注册空的事件处理器");
        return false;
    }

    int fd = handler->getFd();
    if (fd < 0)
    {
        LOG_ERROR("注册无效的文件描述符: {}", fd);
        return false;
    }

    epoll_event ev;
    ev.events = static_cast<uint32_t>(events) | EPOLLET; // 边缘触发
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        LOG_ERROR("添加fd {}到epoll失败: {}", fd, strerror(errno));
        return false;
    }

    handlers_[fd] = handler;
    LOG_DEBUG("注册事件处理器成功，fd: {}", fd);
    return true;
}

bool Reactor::removeHandler(int fd)
{
    auto it = handlers_.find(fd);
    if (it == handlers_.end())
    {
        LOG_WARN("尝试移除不存在的处理器，fd: {}", fd);
        return false;
    }

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0)
    {
        LOG_ERROR("从epoll中移除fd {}失败: {}", fd, strerror(errno));
        // 即使epoll_ctl失败，也要清理内部状态
    }

    handlers_.erase(it);
    LOG_DEBUG("移除事件处理器成功，fd: {}", fd);
    return true;
}

bool Reactor::modifyHandler(int fd, EventType events)
{
    auto it = handlers_.find(fd);
    if (it == handlers_.end())
    {
        LOG_ERROR("尝试修改不存在的处理器，fd: {}", fd);
        return false;
    }

    epoll_event ev;
    ev.events = static_cast<uint32_t>(events) | EPOLLET;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
    {
        LOG_ERROR("修改fd {}的epoll事件失败: {}", fd, strerror(errno));
        return false;
    }

    LOG_DEBUG("修改事件处理器成功，fd: {}", fd);
    return true;
}

void Reactor::setThreadPool(std::shared_ptr<ThreadPool> pool)
{
    thread_pool_ = pool;
    LOG_DEBUG("设置线程池成功");
}

void Reactor::handleEvents()
{
    epoll_event events[MAX_EVENTS];

    int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, EPOLL_TIMEOUT);
    if (nfds < 0)
    {
        if (errno == EINTR)
        {
            return; // 被信号中断，继续
        }
        LOG_ERROR("epoll_wait失败: {}", strerror(errno));
        stop();
        return;
    }

    LOG_DEBUG("epoll_wait返回 {} 个事件", nfds);

    for (int i = 0; i < nfds && running_; ++i)
    {
        processEvent(events[i]);
    }
}

void Reactor::processEvent(const epoll_event &event)
{
    int fd = event.data.fd;
    uint32_t events = event.events;

    auto it = handlers_.find(fd);
    if (it == handlers_.end())
    {
        LOG_WARN("收到未注册fd的事件: {}", fd);
        return;
    }

    auto handler = it->second;
    if (!handler)
    {
        LOG_ERROR("事件处理器为空，fd: {}", fd);
        removeHandler(fd);
        return;
    }

    try
    {
        // 通过按位与操作（&），可以检测 events 是否包含某标志
        // 错误事件优先处理
        if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        {
            LOG_DEBUG("处理错误事件，fd: {}, events: 0x{:x}", fd, events);
            if (thread_pool_)
            {
                // 投递到线程池处理
                postTask([handler]()
                         { handler->handleError(); });
            }
            else
            {
                handler->handleError();
            }
            return;
        }

        // 读事件
        if (events & EPOLLIN)
        {
            LOG_DEBUG("处理读事件，fd: {}", fd);
            if (thread_pool_)
            {
                if (!handler->reading_flag_.test_and_set())
                {
                    postTask([handler]()
                             {
                                 handler->handleRead();
                                 handler->reading_flag_.clear(); // 处理完成后清除标志
                             });
                }
                else
                {
                    LOG_DEBUG("跳过重复的 handleRead 调度，fd: {}", fd);
                }
            }
            else
            {
                if (!handler->reading_flag_.test_and_set())
                {

                    handler->handleRead();
                    handler->reading_flag_.clear(); // 处理完成后清除标志
                }
                else
                {
                    LOG_DEBUG("跳过重复的 handleRead 调度，fd: {}", fd);
                }
            }
        }

        // 写事件
        if (events & EPOLLOUT)
        {
            LOG_DEBUG("处理写事件，fd: {}", fd);
            if (thread_pool_)
            {
                postTask([handler]()
                         { handler->handleWrite(); });
            }
            else
            {
                handler->handleWrite();
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("处理事件时发生异常，fd: {}, 异常: {}", fd, e.what());
        // 异常情况下移除处理器
        removeHandler(fd);
    }
}