#pragma once
#include <condition_variable>
#include <mutex>
#include <stdexcept>
namespace bq {
class Semaphore {
    std::mutex mutex_;
    std::condition_variable cv_;
    int available_, capacity_;

  public:
    explicit Semaphore(int n) : available_(n), capacity_(n) {
        if (n < 1)
            throw std::invalid_argument("Semaphore capacity must be positive");
    }
    void acquire() {
        std::unique_lock<std::mutex> l(mutex_);
        cv_.wait(l, [this] { return available_ > 0; });
        --available_;
    }
    bool tryAcquire() {
        std::lock_guard<std::mutex> l(mutex_);
        if (!available_)
            return false;
        --available_;
        return true;
    }
    void release() {
        std::lock_guard<std::mutex> l(mutex_);
        if (available_ == capacity_)
            throw std::logic_error("Semaphore over-release");
        ++available_;
        cv_.notify_one();
    }
};
} // namespace bq
