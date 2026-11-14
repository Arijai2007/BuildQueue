#pragma once
#include "Job.hpp"
namespace bq {
class MonitoringEngine {
  public:
    static json metrics(const std::vector<Job> &, int tick, int workerCapacityTicks, int busy,
                        int preemptions, int dispatches, int contention, int peakMemory);
};
} // namespace bq
