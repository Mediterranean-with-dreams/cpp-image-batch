#include "threadpool.h"
namespace imagebatch {
ThreadPool::ThreadPool(std::size_t workers, std::size_t capacity) : capacity_(capacity) {
    if (workers == 0 || workers > 64 || capacity == 0)
        throw std::invalid_argument("ThreadPool requires 1..64 workers and a positive queue capacity");
    try {
        workers_.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    } catch (...) {
        stopAndJoin();
        throw;
    }
}
ThreadPool::~ThreadPool() { stopAndJoin(); }
void ThreadPool::stopAndJoin() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    workAvailable_.notify_all();
    spaceAvailable_.notify_all();
    for (auto &worker : workers_) if (worker.joinable()) worker.join();
}
void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            workAvailable_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return; // Drain accepted work before joining.
            task = std::move(queue_.front()); queue_.pop();
            stats_.peakActive = std::max(stats_.peakActive, ++active_);
        }
        spaceAvailable_.notify_one();
        task(); // packaged_task transports user exceptions through the future.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
        }
    }
}
ThreadPool::Stats ThreadPool::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}
}
