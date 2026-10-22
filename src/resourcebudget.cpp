#include "resourcebudget.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>
namespace imagebatch {
ResourceBudget::ResourceBudget(std::uint64_t capacity) : capacity_(capacity) {
    if (!capacity) throw std::invalid_argument("Resource budget must be positive");
}
std::optional<ResourceBudget::Lease> ResourceBudget::acquire(std::uint64_t bytes, const std::atomic_bool &cancelled) {
    if (!bytes || bytes > capacity_) throw std::invalid_argument("Reservation exceeds resource budget");
    std::unique_lock<std::mutex> lock(mutex_);
    // Cancellation need not acquire this mutex or depend on the GUI event loop.
    while (!cancelled.load() && bytes > capacity_ - reserved_)
        available_.wait_for(lock, std::chrono::milliseconds(20));
    if (cancelled.load()) return std::nullopt;
    reserved_ += bytes;
    peak_ = std::max(peak_, reserved_);
    return Lease(this, bytes);
}
void ResourceBudget::release(std::uint64_t bytes) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reserved_ -= bytes;
    }
    available_.notify_all();
}
std::uint64_t ResourceBudget::peakReserved() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_;
}
ResourceBudget::Lease::~Lease() { if (owner_) owner_->release(bytes_); }
ResourceBudget::Lease::Lease(Lease &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), bytes_(other.bytes_) {}
ResourceBudget::Lease &ResourceBudget::Lease::operator=(Lease &&other) noexcept {
    if (this != &other) {
        if (owner_) owner_->release(bytes_);
        owner_ = std::exchange(other.owner_, nullptr); bytes_ = other.bytes_;
    }
    return *this;
}
}
