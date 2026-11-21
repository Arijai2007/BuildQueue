#pragma once
#include "DeadlockDetector.hpp"
#include <mutex>
#include <stdexcept>
namespace bq {
class Semaphore {
    TrackedMutex mutex_;
    int available_, capacity_;

  public:
    Semaphore(int n, DeadlockDetector& detector)
        : mutex_(detector, "semaphore state"), available_(n), capacity_(n) {
        if (n < 1)
            throw std::invalid_argument("Semaphore capacity must be positive");
    }
    bool tryAcquire() {
        std::lock_guard<TrackedMutex> l(mutex_);
        if (!available_)
            return false;
        --available_;
        return true;
    }
    void release() {
        std::lock_guard<TrackedMutex> l(mutex_);
        if (available_ == capacity_)
            throw std::logic_error("Semaphore over-release");
        ++available_;
    }
};
} // namespace bq
