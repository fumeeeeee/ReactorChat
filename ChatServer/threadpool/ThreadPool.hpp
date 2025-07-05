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
    // 防止size_t自动隐式转换为threadpool
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency() * 2);
    ~ThreadPool();

    // 禁用拷贝构造和赋值
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // 提交任务到任务队列
    template <class F, class... Args>   // 可变参数模板，在 此处 F表示可调用对象的类型，Args表示参数类型
    auto enqueue(F &&f, Args &&...args) //  完美转发，可以接受左值、右值、lambda、函数指针、成员函数指针等各种可调用对象和参数
        -> std::future<std::invoke_result_t<F, Args...>>;
    // auto与->组合,返回类型后置
    // std::future<T>：表示一个异步操作的结果,通过 future.get() 在需要时获取任务的返回值（如果任务还没完成会阻塞等待）；
    // std::invoke_result_t<F, Args...>：C++17 类型萃取工具，自动推断 F(Args...) 的返回类型

    // 获取线程池状态
    size_t getThreadCount() const { return threads_.size(); }
    size_t getQueueSize() const;
    bool isRunning() const { return !stop_; }

    // 优雅关闭线程池
    void shutdown();

private:
    // 储存工作线程的vector
    std::vector<std::thread> threads_;

    // 任务队列
    std::queue<std::function<void()>> tasks_;
    // 使用示例: std::function<int(int, double)> 只能存储可以用 (int, double) 调用并返回 int 的对象

    // 同步原语
    mutable std::mutex queue_mutex_; // 即使在 const 成员函数中也允许上锁
    std::condition_variable condition_;

    // 停止标志
    std::atomic<bool> stop_;

    // 工作线程函数
    void worker();
};

// 模板方法实现
template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using return_type = std::invoke_result_t<F, Args...>;

    // 创建packaged_task对象指针task来包装可调用对象,(*task)()进行调用
    // 使用示例: std::packaged_task<int(int, double)>
    std::shared_ptr<std::packaged_task<return_type()>> task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    // get_future返回一个 std::future<T> 对象，允许在将来异步获取任务执行的结果

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 如果线程池已停止，不能添加新任务
        if (stop_)
        {
            throw std::runtime_error("线程池已停止，无法添加新任务");
        }

        // 将任务添加到队列
        tasks_.emplace([task]()
                       { (*task)(); });
    }

    // 通知一个等待的线程
    condition_.notify_one();
    return res;
}