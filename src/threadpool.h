#pragma once
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace imagebatch {
// A fixed set of workers and a bounded FIFO queue. A full queue blocks the
// producer, not the GUI (the batch coordinator is itself a background worker).
class ThreadPool {
public:
    struct Stats { std::size_t peakQueued = 0, peakActive = 0; };
    ThreadPool(std::size_t workers, std::size_t capacity);
    ~ThreadPool();
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    template<class Function>
    auto submit(Function &&function) -> std::future<std::invoke_result_t<Function>> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
        auto future = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            spaceAvailable_.wait(lock, [this] { return stopping_ || queue_.size() < capacity_; });
            if (stopping_) throw std::runtime_error("ThreadPool has stopped");
            queue_.emplace([task] { (*task)(); });
            stats_.peakQueued = std::max(stats_.peakQueued, queue_.size());
        }
        workAvailable_.notify_one();
        return future;
    }
    Stats stats() const;
private:
    void stopAndJoin() noexcept; // Called by the owning thread only.
    void workerLoop();
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable workAvailable_, spaceAvailable_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    std::size_t active_ = 0;
    Stats stats_;
};
}
