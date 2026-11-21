#include "DeadlockDetector.hpp"

#include <exception>
#include <sstream>
#include <utility>

namespace bq {
DeadlockReport DeadlockDetector::report() const {
    std::lock_guard<std::mutex> lock(graphMutex_);
    return report_;
}

TrackedMutex::TrackedMutex(DeadlockDetector& detector, std::string name)
    : detector_(detector), name_(std::move(name)) {}

bool TrackedMutex::closesCycle(std::thread::id requester) const {
    const TrackedMutex* resource = this;
    while (resource->owner_ != std::thread::id{}) {
        if (resource->owner_ == requester)
            return true;
        auto next = detector_.waiting_.find(resource->owner_);
        if (next == detector_.waiting_.end())
            return false;
        resource = next->second;
    }
    return false;
}

std::string TrackedMutex::describeCycle(std::thread::id requester) const {
    std::ostringstream cycle;
    cycle << "Thread " << requester;
    auto resource = this;
    do {
        cycle << " waits for " << resource->name_ << " held by thread " << resource->owner_;
        if (resource->owner_ == requester)
            break;
        resource = detector_.waiting_.at(resource->owner_);
    } while (true);
    return cycle.str();
}

void TrackedMutex::lock() {
    const auto requester = std::this_thread::get_id();
    std::unique_lock<std::mutex> graphLock(detector_.graphMutex_);
    if (owner_ != std::thread::id{}) {
        if (closesCycle(requester)) {
            auto cycle = describeCycle(requester);
            detector_.report_.lastCycle = cycle;
            ++detector_.report_.count;
            throw DeadlockDetected(cycle);
        }
        detector_.waiting_.emplace(requester, this);
        try {
            available_.wait(graphLock, [this] { return owner_ == std::thread::id{}; });
        } catch (...) {
            detector_.waiting_.erase(requester);
            throw;
        }
        detector_.waiting_.erase(requester);
    }
    owner_ = requester;
}

void TrackedMutex::unlock() noexcept {
    {
        std::lock_guard<std::mutex> lock(detector_.graphMutex_);
        if (owner_ != std::this_thread::get_id())
            std::terminate();
        owner_ = {};
    }
    available_.notify_all();
}
}
