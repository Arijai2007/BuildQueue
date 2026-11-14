#include "MemoryManager.hpp"
#include <algorithm>
#include <stdexcept>
namespace bq {
bool MemoryManager::canAllocate(int size) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size > 0 && std::any_of(blocks_.begin(), blocks_.end(), [size](const auto &block) {
               return block.owner == 0 && block.size >= size;
           });
}
MemoryManager::MemoryManager(int capacity) : capacity_(capacity), blocks_{{0, capacity, 0}} {
    if (capacity < 1)
        throw std::invalid_argument("Memory must be positive");
}
int MemoryManager::allocate(int owner, int size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (owner < 1 || size < 1)
        throw std::invalid_argument("Invalid allocation");
    for (const auto &b : blocks_)
        if (b.owner == owner)
            throw std::invalid_argument("Owner already allocated");
    std::size_t chosen = blocks_.size();
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        const auto &b = blocks_[i];
        if (b.owner || b.size < size)
            continue;
        if (chosen == blocks_.size() || (policy_ == "best" && b.size < blocks_[chosen].size) ||
            (policy_ == "worst" && b.size > blocks_[chosen].size))
            chosen = i;
        if (policy_ == "first")
            break;
    }
    if (chosen == blocks_.size())
        return -1;
    auto old = blocks_[chosen];
    blocks_[chosen] = {old.offset, size, owner};
    if (old.size > size)
        blocks_.insert(blocks_.begin() + static_cast<std::ptrdiff_t>(chosen + 1),
                       {old.offset + size, old.size - size, 0});
    return old.offset;
}
bool MemoryManager::release(int owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool found = false;
    for (auto &b : blocks_)
        if (b.owner == owner && owner > 0) {
            b.owner = 0;
            found = true;
        }
    if (!found)
        return false;
    for (std::size_t i = 0; i + 1 < blocks_.size();) {
        if (!blocks_[i].owner && !blocks_[i + 1].owner) {
            blocks_[i].size += blocks_[i + 1].size;
            blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(i + 1));
        } else
            ++i;
    }
    return true;
}
void MemoryManager::policy(const std::string &p) {
    if (p != "first" && p != "best" && p != "worst")
        throw std::invalid_argument("Memory policy must be first, best or worst");
    std::lock_guard<std::mutex> lock(mutex_);
    policy_ = p;
}
void MemoryManager::reset(int cap) {
    if (cap < 1 || cap > 1048576)
        throw std::invalid_argument("Invalid memory capacity");
    std::lock_guard<std::mutex> lock(mutex_);
    capacity_ = cap;
    blocks_ = {{0, cap, 0}};
}
json MemoryManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int free = 0, largest = 0, alloc = 0, holes = 0;
    json blocks = json::array();
    for (const auto &b : blocks_) {
        blocks.push_back({{"offset", b.offset}, {"size", b.size}, {"owner", b.owner}});
        if (!b.owner) {
            free += b.size;
            largest = std::max(largest, b.size);
            ++holes;
        } else
            ++alloc;
    }
    return {{"total", capacity_},
            {"used", capacity_ - free},
            {"free", free},
            {"largest_free", largest},
            {"allocated_blocks", alloc},
            {"free_blocks", holes},
            {"internal_fragmentation", 0},
            {"external_fragmentation",
             free ? 100.0 * (1.0 - static_cast<double>(largest) / free) : 0.0},
            {"utilization", 100.0 * (capacity_ - free) / capacity_},
            {"policy", policy_},
            {"blocks", blocks}};
}
} // namespace bq
