#include "ThreadPool.hpp"
#include "logger/log_macros.hpp"
#include <algorithm>

ThreadPool::ThreadPool(size_t threads) : stop_(false)
{
    // 确保至少有一个线程
    size_t thread_count = std::max(threads, size_t(1));

    LOG_INFO("创建线程池，线程数: {}", thread_count);

    // 创建工作线程
    threads_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i)
    {
        threads_.emplace_back(&ThreadPool::worker, this);
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

void ThreadPool::shutdown()
{
    if (stop_)
    {
        return; // 已经停止
    }

    LOG_INFO("开始关闭线程池...");

    {
        // 加锁可以确保在设置 stop_ 的同时，没有其他线程在修改或访问任务队列等共享资源，保证线程池状态的整体一致性
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }

    // 通知所有工作线程
    condition_.notify_all();

    // 等待所有线程完成
    for (std::thread &thread : threads_)
    {
        if (thread.joinable()) // 判断线程对象是否可以被 join（即是否代表一个活动线程）
        {
            thread.join();
            // 阻塞当前线程，直到被 join 的线程执行完毕
        }
    }

    threads_.clear();

    // 清空任务队列
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        std::queue<std::function<void()>> empty;
        tasks_.swap(empty);
        // 这种做法比逐个弹出队列元素更高效，因为 swap 操作的复杂度为常数（O(1)），而逐个弹出则是线性复杂度（O(n)）
    }

    LOG_INFO("线程池已关闭");
}

size_t ThreadPool::getQueueSize() const
{
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::worker()
{
    [[maybe_unused]] std::thread::id thread_id = std::this_thread::get_id();
    //  LOG_DEBUG("工作线程启动: {}", reinterpret_cast<uintptr_t>(&thread_id));
    //  uintptr_t：无符号整数类型，大小足够存储指针

    while (true)
    {
        std::function<void()> task;

        // 获取任务
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // 等待任务或停止信号
            // condition_.wait(lock, predicate);
            // 被唤醒后一定再次判断条件
            condition_.wait(lock, [this]
                            { return stop_ || !tasks_.empty(); });
            // wait():
            // 释放 mutex（让其他线程可以修改共享数据）
            // 挂起当前线程（进入等待状态）
            // 等待唤醒（notify）+ 条件满足，才重新获得锁并返回 

            // 如果停止且没有任务，退出
            if (stop_ && tasks_.empty())
            {
                break;
            }

            // 获取任务
            if (!tasks_.empty())
            {
                task = std::move(tasks_.front());// std::move() 用于将任务从的所有权转移到 task 变量
                tasks_.pop();
            }
        }

        // 执行任务
        if (task)
        {
            try
            {
                task();
            }
            catch (const std::exception &e)
            {
                // 只捕获标准异常
                LOG_ERROR("工作线程执行任务时发生异常: {}", e.what());
            }
            catch (...)
            {
                // 处理未知异常,确保程序继续运行
                LOG_ERROR("工作线程执行任务时发生未知异常");
            }
        }
    }
}