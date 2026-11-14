#include "JobManager.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace bq {
std::string stateName(JobState s) {
    switch (s) {
    case JobState::NEW:
        return "NEW";
    case JobState::READY:
        return "READY";
    case JobState::RUNNING:
        return "RUNNING";
    case JobState::WAITING:
        return "WAITING";
    case JobState::COMPLETED:
        return "COMPLETED";
    case JobState::CANCELLED:
        return "CANCELLED";
    }
    throw std::logic_error("Unknown job state");
}
void Job::transition(JobState n) {
    bool valid =
        (state == JobState::NEW && (n == JobState::READY || n == JobState::CANCELLED)) ||
        (state == JobState::READY &&
         (n == JobState::RUNNING || n == JobState::WAITING || n == JobState::CANCELLED)) ||
        (state == JobState::WAITING && (n == JobState::READY || n == JobState::CANCELLED)) ||
        (state == JobState::RUNNING && (n == JobState::READY || n == JobState::COMPLETED));
    if (!valid)
        throw std::invalid_argument("Invalid transition " + stateName(state) + " -> " +
                                    stateName(n));
    state = n;
}
json Job::snapshot() const {
    json j = {{"id", id},
              {"name", spec.name},
              {"type", spec.type},
              {"arrival", spec.arrival},
              {"priority", spec.priority},
              {"burst", spec.burst},
              {"remaining", remaining},
              {"memory", spec.memory},
              {"state", stateName(state)},
              {"first_run", firstRun},
              {"completion", completion},
              {"ready_wait", readyWait},
              {"resource_wait", resourceWait},
              {"worker", worker},
              {"memory_offset", memoryOffset},
              {"wait_reason", waitReason},
              {"process_exit", processExit},
              {"process_pid", processPid},
              {"process_wall_ms", processWallMs},
              {"process_error", processError}};
    j["turnaround"] = completion < 0 ? json(nullptr) : json(completion - spec.arrival);
    j["response"] = firstRun < 0 ? json(nullptr) : json(firstRun - spec.arrival);
    return j;
}
int integer(const json &j, const std::string &k, int d) {
    if (!j.contains(k))
        return d;
    if (!j[k].is_number_integer())
        throw std::invalid_argument(k + " must be an integer");
    if (j[k].is_number_unsigned() &&
        j[k].get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument(k + " is out of range");
    auto n = j[k].get<std::int64_t>();
    if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
        throw std::invalid_argument(k + " is out of range");
    return static_cast<int>(n);
}
JobSpec parseJob(const json &j) {
    if (!j.is_object())
        throw std::invalid_argument("Job must be a JSON object");
    JobSpec s;
    s.name = j.value("name", s.name);
    s.type = j.value("type", s.type);
    s.arrival = integer(j, "arrival", 0);
    s.priority = integer(j, "priority", 3);
    s.burst = integer(j, "burst", 4);
    s.memory = integer(j, "memory", 128);
    return s;
}
void JobManager::validate(const JobSpec &s, int cap, const std::string &mode) {
    if (s.name.empty() || s.name.size() > 80 ||
        std::any_of(s.name.begin(), s.name.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::invalid_argument("Name must contain 1–80 printable characters");
    if (s.arrival < 0 || s.arrival > 100000 || s.burst < 1 || s.burst > 10000 || s.priority < 0 ||
        s.priority > 99 || s.memory < 1 || s.memory > cap)
        throw std::invalid_argument(
            "Require arrival 0–100000, burst 1–10000, priority 0–99 and memory 1–capacity MB");
    if (s.type != "simulated" && s.type != "process")
        throw std::invalid_argument("Unknown job type");
    if (s.type == "process" && mode != "live")
        throw std::invalid_argument("Process jobs require live mode");
}
int JobManager::submit(const JobSpec &s) {
    if (nextId_ == std::numeric_limits<int>::max())
        throw std::overflow_error("Job ID space exhausted; restart the engine");
    Job j;
    j.id = nextId_++;
    j.spec = s;
    j.remaining = s.burst;
    jobs_.push_back(j);
    return j.id;
}
Job &JobManager::at(int id) {
    for (auto &j : jobs_)
        if (j.id == id)
            return j;
    throw std::out_of_range("Job does not exist");
}
void JobManager::reset() {
    jobs_.clear(); /* IDs remain unique for the engine lifetime. */
}
} // namespace bq
