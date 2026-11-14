#include "SyncController.hpp"
#include <algorithm>
namespace bq {
bool SyncController::acquire() {
    if (!slots_->tryAcquire())
        return false;
    ++active_;
    peak_ = std::max(peak_, active_);
    return true;
}
void SyncController::release() {
    if (active_ <= 0)
        throw std::logic_error("No slot to release");
    --active_;
    slots_->release();
}
void SyncController::configure(int n) {
    if (n < 1 || n > 32)
        throw std::invalid_argument("Slots must be 1–32");
    if (active_)
        throw std::invalid_argument("Slots require no running jobs");
    slots_ = std::make_unique<Semaphore>(n);
    capacity_ = n;
    peak_ = 0;
}
json SyncController::snapshot() const {
    return {{"slots", capacity_},
            {"active_slots", active_},
            {"available_slots", capacity_ - active_},
            {"peak_slots", peak_},
            {"shared_counter", counter_.load()}};
}
} // namespace bq
