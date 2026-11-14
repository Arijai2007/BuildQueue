#pragma once
#include "Job.hpp"
namespace bq {
// Protected by BuildQueueEngine's transaction mutex; no escaping mutable references.
class JobManager {
    int nextId_ = 1;
    std::vector<Job> jobs_;

  public:
    static void validate(const JobSpec &, int capacity, const std::string &mode);
    int submit(const JobSpec &);
    Job &at(int id);
    const std::vector<Job> &jobs() const {
        return jobs_;
    }
    std::vector<Job> &mutableJobs() {
        return jobs_;
    }
    void reset();
};
} // namespace bq
