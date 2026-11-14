#include "SchedulerEngine.hpp"
#include <algorithm>
namespace bq {
std::vector<int> SchedulerEngine::order(const std::vector<Job> &jobs) const {
    std::vector<const Job *> candidates;
    for (const auto &j : jobs)
        if (j.state == JobState::READY)
            candidates.push_back(&j);
    std::stable_sort(candidates.begin(), candidates.end(),
                     [this](auto a, auto b) { return policy_->before(*a, *b); });
    std::vector<int> ids;
    for (auto j : candidates)
        ids.push_back(j->id);
    return ids;
}
} // namespace bq
