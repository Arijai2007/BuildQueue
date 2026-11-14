#include "MonitoringEngine.hpp"
namespace bq {
json MonitoringEngine::metrics(const std::vector<Job> &jobs, int tick, int workerCapacityTicks,
                               int busy, int preempt, int dispatch, int contention, int peak) {
    int completed = 0, running = 0, waiting = 0, cancelled = 0;
    double ready = 0, blocked = 0, turn = 0, response = 0;
    for (const auto &j : jobs) {
        if (j.state == JobState::COMPLETED) {
            ++completed;
            ready += j.readyWait;
            blocked += j.resourceWait;
            turn += j.completion - j.spec.arrival;
            response += j.firstRun - j.spec.arrival;
        }
        if (j.state == JobState::RUNNING)
            ++running;
        if (j.state == JobState::WAITING)
            ++waiting;
        if (j.state == JobState::CANCELLED)
            ++cancelled;
    }
    return {{"total", jobs.size()},
            {"completed", completed},
            {"running", running},
            {"waiting", waiting},
            {"cancelled", cancelled},
            {"tick", tick},
            {"avg_wait", completed ? ready / completed : 0},
            {"avg_resource_wait", completed ? blocked / completed : 0},
            {"avg_turnaround", completed ? turn / completed : 0},
            {"avg_response", completed ? response / completed : 0},
            {"cpu_utilization", workerCapacityTicks ? 100.0 * busy / workerCapacityTicks : 0},
            {"throughput", tick ? static_cast<double>(completed) / tick : 0},
            {"busy_worker_ticks", busy},
            {"worker_capacity_ticks", workerCapacityTicks},
            {"preemptions", preempt},
            {"dispatches", dispatch},
            {"context_switch_like_transitions", dispatch},
            {"contention_events", contention},
            {"peak_memory", peak},
            {"completed_sample_count", completed}};
}
} // namespace bq
