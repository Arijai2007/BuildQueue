#include "SchedulingPolicy.hpp"
#include <stdexcept>
#include <tuple>
namespace bq {
class Fcfs final : public SchedulingPolicy {
  public:
    std::string name() const override {
        return "fcfs";
    }
    bool before(const Job &a, const Job &b) const override {
        return std::tie(a.spec.arrival, a.id) < std::tie(b.spec.arrival, b.id);
    }
    std::string reason(const Job &) const override {
        return "earliest arrival among eligible jobs (ID breaks ties)";
    }
};
class Sjf final : public SchedulingPolicy {
  public:
    std::string name() const override {
        return "sjf";
    }
    bool before(const Job &a, const Job &b) const override {
        return std::tie(a.spec.burst, a.spec.arrival, a.id) <
               std::tie(b.spec.burst, b.spec.arrival, b.id);
    }
    std::string reason(const Job &) const override {
        return "shortest original CPU burst among eligible jobs; non-preemptive";
    }
};
class Priority final : public SchedulingPolicy {
  public:
    std::string name() const override {
        return "priority";
    }
    bool before(const Job &a, const Job &b) const override {
        return std::tie(a.spec.priority, a.spec.arrival, a.id) <
               std::tie(b.spec.priority, b.spec.arrival, b.id);
    }
    std::string reason(const Job &) const override {
        return "lowest numeric priority among eligible jobs; non-preemptive";
    }
};
class RoundRobin final : public SchedulingPolicy {
  public:
    std::string name() const override {
        return "rr";
    }
    bool before(const Job &a, const Job &b) const override {
        return std::tie(a.readyOrder, a.id) < std::tie(b.readyOrder, b.id);
    }
    std::string reason(const Job &) const override {
        return "head of the FIFO ready queue; bounded by the configured quantum";
    }
};
std::unique_ptr<SchedulingPolicy> makePolicy(const std::string &p) {
    if (p == "fcfs")
        return std::make_unique<Fcfs>();
    if (p == "sjf")
        return std::make_unique<Sjf>();
    if (p == "priority")
        return std::make_unique<Priority>();
    if (p == "rr")
        return std::make_unique<RoundRobin>();
    throw std::invalid_argument("Policy must be fcfs, sjf, priority or rr");
}
} // namespace bq
