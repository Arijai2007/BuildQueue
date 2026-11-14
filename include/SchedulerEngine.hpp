#pragma once
#include "SchedulingPolicy.hpp"
namespace bq {
class SchedulerEngine {
    std::unique_ptr<SchedulingPolicy> policy_ = makePolicy("fcfs");

  public:
    void policy(const std::string &p) {
        policy_ = makePolicy(p);
    }
    std::string name() const {
        return policy_->name();
    }
    std::vector<int> order(const std::vector<Job> &) const;
    std::string reason(const Job &j) const {
        return policy_->reason(j);
    }
};
} // namespace bq
