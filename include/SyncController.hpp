#pragma once
#include "Job.hpp"
#include "Semaphore.hpp"
#include <atomic>
#include <memory>
namespace bq {
// Admission called under engine transaction lock. Completed work uses atomic accounting.
class SyncController {
    std::unique_ptr<Semaphore> slots_;
    int capacity_, active_ = 0, peak_ = 0;
    std::atomic<std::uint64_t> counter_{0};

  public:
    explicit SyncController(int n = 2) : slots_(std::make_unique<Semaphore>(n)), capacity_(n) {}
    bool acquire();
    bool hasAvailableSlot() const {
        return active_ < capacity_;
    }
    void release();
    void configure(int);
    void clearCounter() {
        counter_ = 0;
        peak_ = 0;
    }
    void increment() {
        counter_.fetch_add(1, std::memory_order_relaxed);
    }
    json snapshot() const;
};
} // namespace bq
