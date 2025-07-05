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
        // LOG_DEBUG("创建工作线程 {}", i);
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
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }

    // 通知所有工作线程
    condition_.notify_all();

    // 等待所有线程完成
    for (std::thread &thread : threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    threads_.clear();

    // 清空任务队列
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        std::queue<std::function<void()>> empty;
        tasks_.swap(empty);
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
    std::thread::id thread_id = std::this_thread::get_id();
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

            // 如果停止且没有任务，退出
            if (stop_ && tasks_.empty())
            {
                break;
            }

            // 获取任务
            if (!tasks_.empty())
            {
                task = std::move(tasks_.front());
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

    LOG_DEBUG("工作线程退出: {}", reinterpret_cast<uintptr_t>(&thread_id));
}