#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

class ThreadPool
{
public:
    //防止size_t自动隐式转换为threadpool
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency()*2);
    ~ThreadPool();

    // 禁用拷贝构造和赋值
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // 提交任务到线程池
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // 获取线程池状态
    size_t getThreadCount() const { return threads_.size(); }
    size_t getQueueSize() const;
    bool isRunning() const { return !stop_; }

    // 优雅关闭线程池
    void shutdown();

private:
    // 工作线程
    std::vector<std::thread> threads_;

    // 任务队列
    std::queue<std::function<void()>> tasks_;

    // 同步原语
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;

    // 停止标志
    std::atomic<bool> stop_;

    // 工作线程函数
    void worker();
};

// 模板方法实现
template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args)
    -> std::future<std::invoke_result_t<F, Args...>>//使用std::future<>实现异步任务提交返回值
{

    using return_type = std::invoke_result_t<F, Args...>;

    // 创建打包任务
    // std::packeage_task用于包装可调用对象
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 如果线程池已停止，不能添加新任务
        if (stop_)
        {
            throw std::runtime_error("线程池已停止，无法添加新任务");
        }

        // 将任务添加到队列
        tasks_.emplace([task]()
                        { 
                            (*task)();
                        });
    }

    // 通知一个等待的线程
    condition_.notify_one();
    return res;
}