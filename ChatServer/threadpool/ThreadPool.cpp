#include "ThreadPool.hpp"
#include "logger/log_macros.hpp"

ThreadPool::ThreadPool(size_t numThreads) : stop_(false) {
    LOG_INFO("线程池初始化，线程数: {}", numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this, i] {
            LOG_DEBUG("工作线程 {} 启动", i);
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) {
                        LOG_DEBUG("工作线程 {} 退出", i);
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                LOG_DEBUG("工作线程 {} 执行任务", i);
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (size_t i = 0; i < workers_.size(); ++i) {
        if (workers_[i].joinable()) {
            workers_[i].join();
            LOG_DEBUG("工作线程 {} 已回收", i);
        }
    }
    LOG_INFO("线程池已销毁");
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        tasks_.emplace(std::move(task));
        LOG_DEBUG("新任务加入队列，当前队列长度: {}", tasks_.size());
    }
    condition_.notify_one();
}
