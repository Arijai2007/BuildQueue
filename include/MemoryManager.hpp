#pragma once
#include "Job.hpp"
#include <mutex>
namespace bq {
struct MemoryBlock {
    int offset, size, owner;
};
class MemoryManager {
    mutable std::mutex mutex_;
    int capacity_;
    std::string policy_ = "first";
    std::vector<MemoryBlock> blocks_;

  public:
    explicit MemoryManager(int capacity = 1024);
    int allocate(int owner, int size);
    bool canAllocate(int size) const;
    bool release(int owner);
    void policy(const std::string &);
    json snapshot() const;
    void reset(int capacity);
};
} // namespace bq
