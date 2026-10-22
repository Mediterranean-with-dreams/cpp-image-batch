#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace imagebatch {
// Limits reservations for estimated image working buffers. This is not an OS
// memory quota: Qt/codecs, the GUI and allocator overhead have separate costs.
class ResourceBudget {
public:
    class Lease {
    public:
        ~Lease();
        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
    private:
        friend class ResourceBudget;
        Lease(ResourceBudget *owner, std::uint64_t bytes) : owner_(owner), bytes_(bytes) {}
        ResourceBudget *owner_;
        std::uint64_t bytes_;
    };
    explicit ResourceBudget(std::uint64_t capacity);
    std::optional<Lease> acquire(std::uint64_t bytes, const std::atomic_bool &cancelled);
    std::uint64_t peakReserved() const;
private:
    void release(std::uint64_t bytes);
    const std::uint64_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::uint64_t reserved_ = 0, peak_ = 0;
};
}
