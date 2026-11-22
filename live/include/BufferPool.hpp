#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace bq {
enum class Fit { First, Best, Worst };

class BufferPool {
public:
    using Clock = std::chrono::steady_clock;
    class WaitTimeout : public std::runtime_error {
    public:
        WaitTimeout() : std::runtime_error("Timed out waiting for a checksum buffer.") {}
    };
    static constexpr std::size_t maxBuffer = 256 * 1024;
    struct Block {
        std::size_t offset, size;
        bool used;
    };
    struct Stats {
        std::vector<Block> blocks;
        std::size_t free, largest, peak, merges;
        double fragmentation, peakFragmentation;
    };
    class Lease {
        friend class BufferPool;
        BufferPool& pool_;
        std::size_t offset_, size_;

        Lease(BufferPool& pool, std::size_t offset, std::size_t size)
            : pool_(pool), offset_(offset), size_(size) {}

    public:
        ~Lease() { pool_.release(offset_); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        char* data() const { return pool_.storage_.data() + offset_; }
        std::size_t size() const { return size_; }
    };

    explicit BufferPool(std::size_t capacity = 2 * maxBuffer)
        : storage_(capacity), blocks_{{0, capacity, false}} {
        if (!capacity)
            throw std::invalid_argument("Buffer pool requires positive capacity");
        // Reserve metadata for the normal checksum workload.
        blocks_.reserve(2 + capacity / 4096);
    }
    void setFit(Fit fit) {
        std::lock_guard<std::mutex> guard(mutex_);
        fit_ = fit;
    }
    Lease acquire(std::size_t requested, Clock::time_point deadline = Clock::time_point::max()) {
        if (!requested || requested > storage_.size())
            throw std::invalid_argument("Invalid buffer size");
        std::unique_lock<std::mutex> lock(mutex_);
        std::size_t index;
        const auto hasBlock = [&] {
            index = blocks_.size();
            for (std::size_t i = 0; i < blocks_.size(); ++i) {
                const auto& block = blocks_[i];
                if (block.used || block.size < requested)
                    continue;
                if (index == blocks_.size() ||
                    (fit_ == Fit::Best && block.size < blocks_[index].size) ||
                    (fit_ == Fit::Worst && block.size > blocks_[index].size))
                    index = i;
                if (fit_ == Fit::First)
                    break;
            }
            return index < blocks_.size();
        };
        if (deadline == Clock::time_point::max())
            available_.wait(lock, hasBlock);
        else if (!available_.wait_until(lock, deadline, hasBlock))
            throw WaitTimeout();
        const auto block = blocks_[index];
        if (block.size > requested)
            blocks_.insert(blocks_.begin() + index + 1,
                           {block.offset + requested, block.size - requested, false});
        blocks_[index] = {block.offset, requested, true};
        used_ += requested;
        peak_ = std::max(peak_, used_);
        updateFragmentation();
        return Lease(*this, block.offset, requested);
    }
    Stats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto free = storage_.size() - used_;
        return {blocks_, free, largestFree_, peak_, merges_, fragmentation(free), peakFragmentation_};
    }

private:
    void release(std::size_t offset) noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto block = std::find_if(blocks_.begin(), blocks_.end(),
                                      [offset](const Block& b) { return b.offset == offset; });
            used_ -= block->size;
            block->used = false;
            if (block != blocks_.begin() && !(block - 1)->used) {
                (block - 1)->size += block->size;
                block = blocks_.erase(block) - 1;
                ++merges_;
            }
            if (block + 1 != blocks_.end() && !(block + 1)->used) {
                block->size += (block + 1)->size;
                blocks_.erase(block + 1);
                ++merges_;
            }
            updateFragmentation();
        }
        available_.notify_all();
    }
    double fragmentation(std::size_t free) const {
        return free ? 100.0 * (1.0 - static_cast<double>(largestFree_) / free) : 0;
    }
    void updateFragmentation() {
        largestFree_ = 0;
        for (const auto& block : blocks_)
            if (!block.used)
                largestFree_ = std::max(largestFree_, block.size);
        peakFragmentation_ = std::max(peakFragmentation_, fragmentation(storage_.size() - used_));
    }

    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<char> storage_;
    std::vector<Block> blocks_;
    Fit fit_ = Fit::First;
    std::size_t used_ = 0, largestFree_ = storage_.size(), peak_ = 0, merges_ = 0;
    double peakFragmentation_ = 0;
};
}
