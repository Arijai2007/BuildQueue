#pragma once
#include "Job.hpp"
#include <memory>
namespace bq {
class SchedulingPolicy {
  public:
    virtual ~SchedulingPolicy() = default;
    virtual std::string name() const = 0;
    virtual bool before(const Job &, const Job &) const = 0;
    virtual std::string reason(const Job &) const = 0;
};
std::unique_ptr<SchedulingPolicy> makePolicy(const std::string &);
} // namespace bq
