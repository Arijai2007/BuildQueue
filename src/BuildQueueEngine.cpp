#include "BuildQueueEngine.hpp"
#include "Scenario.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace bq {
BuildQueueEngine::BuildQueueEngine(std::string root, std::string logFile)
    : logger_(logFile), root_(std::move(root)) {
    coordinator_ = std::thread([this] { coordinate(); });
}
BuildQueueEngine::~BuildQueueEngine() {
    {
        std::lock_guard<std::mutex> l(mutex_);
        stopping_ = true;
        running_ = false;
    }
    cv_.notify_all();
    if (coordinator_.joinable())
        coordinator_.join();
    // pool_ joins before synchronization and memory members are destroyed.
}
void BuildQueueEngine::event(const std::string &type, int id, const std::string &message) {
    logger_.record(clock_.now(), type, id, message);
    explanation_ = message;
}
void BuildQueueEngine::readyLocked(Job &j, const std::string &type) {
    j.transition(JobState::READY);
    j.readyOrder = ++readySequence_;
    j.waitReason.clear();
    event(type, j.id, "Job " + std::to_string(j.id) + " joined READY.");
}
void BuildQueueEngine::arrivalsLocked() {
    std::vector<Job *> arrivals;
    for (auto &j : jobs_.mutableJobs())
        if (j.state == JobState::NEW && j.spec.arrival <= clock_.now())
            arrivals.push_back(&j);
    std::sort(arrivals.begin(), arrivals.end(), [](auto a, auto b) {
        return std::tie(a->spec.arrival, a->id) < std::tie(b->spec.arrival, b->id);
    });
    for (auto j : arrivals)
        readyLocked(*j);
}
bool BuildQueueEngine::unfinishedLocked() const {
    for (const auto &j : jobs_.jobs())
        if (j.state != JobState::COMPLETED && j.state != JobState::CANCELLED)
            return true;
    return false;
}
int BuildQueueEngine::submit(const JobSpec &input, bool arrivalNow) {
    std::lock_guard<std::mutex> l(mutex_);
    JobSpec s = input;
    if (arrivalNow)
        s.arrival = clock_.now();
    JobManager::validate(s, capacity_, mode_);
    if (jobs_.jobs().size() >= 500)
        throw std::invalid_argument("Maximum 500 jobs per experiment");
    if (s.arrival < clock_.now())
        throw std::invalid_argument("Arrival cannot precede the current experiment tick");
    int id = jobs_.submit(s);
    ++experimentRevision_;
    scenario_.clear();
    event("JOB_SUBMITTED", id, "Submitted " + s.name + ".");
    arrivalsLocked();
    cv_.notify_all();
    return id;
}
void BuildQueueEngine::releaseLocked(Job &j) {
    if (j.memoryOffset >= 0) {
        if (!memory_.release(j.id))
            throw std::logic_error("Memory ownership invariant failed");
        j.memoryOffset = -1;
        event("MEMORY_RELEASED", j.id, "Memory released and adjacent holes coalesced.");
    }
}
void BuildQueueEngine::cancel(int id) {
    std::lock_guard<std::mutex> l(mutex_);
    auto &j = jobs_.at(id);
    j.transition(JobState::CANCELLED);
    ++experimentRevision_;
    releaseLocked(j);
    event("JOB_CANCELLED", id, "Job cancelled before execution or between quanta.");
    cv_.notify_all();
}
void BuildQueueEngine::policy(const std::string &p, int q) {
    auto validated = makePolicy(p);
    (void)validated;
    if (q < 1 || q > 1000)
        throw std::invalid_argument("Quantum must be 1–1000");
    std::lock_guard<std::mutex> l(mutex_);
    if (mode_ == "live" && p == "rr")
        throw std::invalid_argument("Live execution uses non-preemptive policies");
    scheduler_.policy(p);
    ++experimentRevision_;
    quantum_ = q;
    for (auto &j : jobs_.mutableJobs())
        j.quantumUsed = 0;
    event("POLICY_CHANGED", 0,
          "Scheduling policy: " + p +
              ". Active jobs retain their lanes; quantum counters restart.");
}
void BuildQueueEngine::memoryPolicy(const std::string &p) {
    std::lock_guard<std::mutex> l(mutex_);
    memory_.policy(p);
    ++experimentRevision_;
    event("POLICY_CHANGED", 0, "Memory allocation policy: " + p + ".");
}
void BuildQueueEngine::configLocked(const json &c) {
    if (!c.is_object())
        throw std::invalid_argument("Configuration must be an object");
    int w = integer(c, "workers", workers_), s = integer(c, "slots", slots_),
        cap = integer(c, "memory", capacity_), d = integer(c, "delay_ms", delayMs_);
    std::string mode = c.value("mode", mode_);
    if (w < 1 || w > 16 || s < 1 || s > 32 || cap < 1 || cap > 1048576 || d < 0 || d > 5000 ||
        (mode != "simulation" && mode != "live"))
        throw std::invalid_argument("Invalid workers, slots, capacity, delay or mode");
    if (running_ && (w != workers_ || s != slots_ || cap != capacity_ || mode != mode_))
        throw std::invalid_argument("Pause before changing resources");
    if ((w != workers_ || s != slots_) &&
        std::any_of(lanes_.begin(), lanes_.end(), [](int id) { return id != 0; }))
        throw std::invalid_argument(
            "Resize only when no job owns a running lane (reset or finish first)");
    if ((cap != capacity_ || mode != mode_) && !jobs_.jobs().empty())
        throw std::invalid_argument("Reset before changing memory capacity or mode");
    if (mode == "live" && scheduler_.name() == "rr")
        throw std::invalid_argument("Select a non-preemptive policy before live mode");
    const bool changed = w != workers_ || s != slots_ || cap != capacity_ || mode != mode_;
    if (w != workers_) {
        auto replacement = std::make_unique<WorkerManager>(w);
        pool_.swap(replacement);
        workers_ = w;
        lanes_.assign(static_cast<std::size_t>(w), 0);
    }
    if (s != slots_) {
        sync_.configure(s);
        slots_ = s;
    }
    if (cap != capacity_) {
        memory_.reset(cap);
        capacity_ = cap;
    }
    mode_ = mode;
    delayMs_ = d;
    if (changed)
        ++experimentRevision_;
    event("CONFIG_CHANGED", 0, "Experiment configuration updated.");
    cv_.notify_all();
}
void BuildQueueEngine::configure(const json &c) {
    std::lock_guard<std::mutex> l(mutex_);
    configLocked(c);
}
bool BuildQueueEngine::stepLocked() {
    if (!unfinishedLocked())
        return false;
    if (clock_.now() >= 500000)
        throw std::runtime_error("Experiment exceeded 500000 logical ticks; reset required");
    arrivalsLocked();
    // Wake only when both resources are potentially available. A blocked job must
    // not accrue READY time merely because all worker lanes happen to be busy.
    for (auto &j : jobs_.mutableJobs())
        if (j.state == JobState::WAITING && sync_.hasAvailableSlot() &&
            (j.memoryOffset >= 0 || memory_.canAllocate(j.spec.memory)))
            readyLocked(j, "RESOURCE_RETRY");
    auto order = scheduler_.order(jobs_.jobs());
    for (int lane = 0; lane < workers_; ++lane) {
        if (lanes_[static_cast<std::size_t>(lane)])
            continue;
        for (int id : order) {
            auto &j = jobs_.at(id);
            if (j.state != JobState::READY)
                continue;
            std::string denied;
            if (!sync_.acquire())
                denied = "RESOURCE_UNAVAILABLE";
            else if (j.memoryOffset < 0) {
                int offset = memory_.allocate(j.id, j.spec.memory);
                if (offset < 0) {
                    sync_.release();
                    denied = "MEMORY_UNAVAILABLE";
                } else {
                    j.memoryOffset = offset;
                    event("MEMORY_ALLOCATED", id,
                          "Reserved " + std::to_string(j.spec.memory) + " MB at offset " +
                              std::to_string(offset) + ".");
                    peakMemory_ = std::max(peakMemory_, memory_.snapshot()["used"].get<int>());
                }
            }
            if (!denied.empty()) {
                j.transition(JobState::WAITING);
                j.waitReason = denied;
                ++contention_;
                event(denied == "MEMORY_UNAVAILABLE" ? "MEMORY_DENIED" : "RESOURCE_WAIT", id,
                      "Job " + std::to_string(id) + " WAITING: " +
                          (denied == "MEMORY_UNAVAILABLE"
                               ? "no contiguous " + std::to_string(j.spec.memory) + " MB block."
                               : "all build slots are occupied."));
                continue;
            }
            event("RESOURCE_ACQUIRED", id, "Acquired a shared build slot.");
            j.transition(JobState::RUNNING);
            j.worker = lane;
            j.waitReason.clear();
            j.quantumUsed = 0;
            if (j.firstRun < 0) {
                j.firstRun = clock_.now();
                event("JOB_STARTED", id, "First execution of job " + std::to_string(id) + ".");
            } else
                event("JOB_RESUMED", id, "Resumed job " + std::to_string(id) + ".");
            lanes_[static_cast<std::size_t>(lane)] = id;
            j.dispatchSerial = ++dispatches_;
            event("JOB_DISPATCHED", id,
                  "Job " + std::to_string(id) + " selected: " + scheduler_.reason(j) + ".");
            break;
        }
    }
    std::vector<std::pair<int, std::future<WorkResult>>> work;
    for (int lane = 0; lane < workers_; ++lane) {
        int id = lanes_[static_cast<std::size_t>(lane)];
        // Keep each tick as a separate slice so quantum boundaries remain visible.
        gantt_.push_back({{"worker", lane},
                          {"job", id},
                          {"start", clock_.now()},
                          {"end", clock_.now() + 1},
                          {"dispatch", id ? jobs_.at(id).dispatchSerial : 0}});
        if (!id)
            continue;
        auto &j = jobs_.at(id);
        bool proc = j.spec.type == "process" && j.remaining == j.spec.burst;
        bool live = mode_ == "live";
        std::string input = root_ + "/demos/process_input.txt";
        work.emplace_back(id, pool_->submit([this, proc, live, input] {
            auto result = WorkerManager::execute(proc, live, input);
            sync_.increment();
            return result;
        }));
    }
    // Workers never acquire the engine mutex. Futures are joined in fixed lane order.
    for (auto &task : work) {
        auto result = task.second.get();
        auto &j = jobs_.at(task.first);
        if (j.spec.type == "process" && j.remaining == j.spec.burst) {
            j.processExit = result.exitCode;
            j.processPid = result.pid;
            j.processWallMs = result.wallMs;
            j.processError = result.error;
            event(result.exitCode ? "PROCESS_ERROR" : "PROCESS_EXITED", j.id,
                  "Controlled cksum PID " + std::to_string(result.pid) + ", exit " +
                      std::to_string(result.exitCode) + ". " + result.error);
        }
        --j.remaining;
        ++j.quantumUsed;
        ++busy_;
    }
    for (auto &j : jobs_.mutableJobs()) {
        if (j.state == JobState::READY)
            ++j.readyWait;
        else if (j.state == JobState::WAITING)
            ++j.resourceWait;
    }
    workerCapacityTicks_ += workers_;
    clock_.advance();
    // Arrivals at a quantum boundary precede requeued unfinished jobs.
    arrivalsLocked();
    for (int lane = 0; lane < workers_; ++lane) {
        int id = lanes_[static_cast<std::size_t>(lane)];
        if (!id)
            continue;
        auto &j = jobs_.at(id);
        if (j.remaining == 0) {
            j.transition(JobState::COMPLETED);
            j.completion = clock_.now();
            j.worker = -1;
            sync_.release();
            lanes_[static_cast<std::size_t>(lane)] = 0;
            event("RESOURCE_RELEASED", id, "Build slot released.");
            releaseLocked(j);
            event("JOB_COMPLETED", id,
                  "Job " + std::to_string(id) + " completed at tick " +
                      std::to_string(clock_.now()) + ".");
            cv_.notify_all();
        } else if (scheduler_.name() == "rr" && j.quantumUsed >= quantum_) {
            sync_.release();
            j.worker = -1;
            lanes_[static_cast<std::size_t>(lane)] = 0;
            ++preemptions_;
            event("RESOURCE_RELEASED", id,
                  "Quantum boundary: released slot; retained simulated memory.");
            readyLocked(j, "JOB_PREEMPTED");
        }
    }
    return true;
}
bool BuildQueueEngine::step() {
    std::lock_guard<std::mutex> l(mutex_);
    if (running_)
        throw std::invalid_argument("Pause before single stepping");
    return stepLocked();
}
void BuildQueueEngine::coordinate() {
    std::unique_lock<std::mutex> l(mutex_);
    while (!stopping_) {
        cv_.wait(l, [this] { return stopping_ || running_; });
        if (stopping_)
            break;
        try {
            if (!stepLocked() || !unfinishedLocked())
                running_ = false;
        } catch (const std::exception &e) {
            running_ = false;
            explanation_ = std::string("Execution error: ") + e.what();
            try {
                logger_.record(clock_.now(), "ERROR", 0, explanation_);
            } catch (...) { /* Preserve the first failure in snapshot. */
            }
        }
        if (running_)
            cv_.wait_for(l, std::chrono::milliseconds(std::max(1, delayMs_)),
                         [this] { return stopping_ || !running_; });
    }
}
void BuildQueueEngine::run() {
    std::lock_guard<std::mutex> l(mutex_);
    running_ = unfinishedLocked();
    cv_.notify_all();
}
void BuildQueueEngine::pause() {
    std::lock_guard<std::mutex> l(mutex_);
    running_ = false;
    cv_.notify_all();
}
void BuildQueueEngine::runUntilDone(int limit) {
    if (limit < 0)
        throw std::invalid_argument("Tick budget cannot be negative");
    pause();
    for (int n = 0; n < limit; ++n)
        if (!step())
            return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (unfinishedLocked())
        throw std::runtime_error("Run exceeded maximum ticks");
}
void BuildQueueEngine::resetLocked() {
    ++experimentRevision_;
    scenario_.clear();
    running_ = false;
    for (auto &j : jobs_.mutableJobs()) {
        if (j.state == JobState::RUNNING)
            sync_.release();
        releaseLocked(j);
    }
    jobs_.reset();
    memory_.reset(capacity_);
    clock_.reset();
    sync_.clearCounter();
    lanes_.assign(static_cast<std::size_t>(workers_), 0);
    gantt_ = json::array();
    busy_ = workerCapacityTicks_ = preemptions_ = dispatches_ = contention_ = peakMemory_ = 0;
    readySequence_ = 0;
    logger_.clear();
    event("RESET", 0, "Experiment reset. Configuration preserved; job IDs remain unique.");
    cv_.notify_all();
}
void BuildQueueEngine::reset() {
    std::lock_guard<std::mutex> l(mutex_);
    resetLocked();
}
void BuildQueueEngine::clearLog() {
    std::lock_guard<std::mutex> l(mutex_);
    logger_.clear();
}
json BuildQueueEngine::configSnapshotLocked() const {
    return {{"policy", scheduler_.name()},
            {"memory_policy", memory_.snapshot()["policy"]},
            {"quantum", quantum_},
            {"workers", workers_},
            {"slots", slots_},
            {"memory", capacity_},
            {"delay_ms", delayMs_},
            {"mode", mode_}};
}
json BuildQueueEngine::snapshot() const {
    std::lock_guard<std::mutex> l(mutex_);
    json jobs = json::array();
    for (const auto &j : jobs_.jobs())
        jobs.push_back(j.snapshot());
    return {
        {"jobs", jobs},
        {"experiment_revision", experimentRevision_},
        {"scenario", scenario_},
        {"config", configSnapshotLocked()},
        {"running", running_},
        {"explanation", explanation_},
        {"memory", memory_.snapshot()},
        {"resources", sync_.snapshot()},
        {"stats", MonitoringEngine::metrics(jobs_.jobs(), clock_.now(), workerCapacityTicks_, busy_,
                                            preemptions_, dispatches_, contention_, peakMemory_)},
        {"gantt", gantt_},
        {"log", logger_.snapshot()},
        {"logging", logger_.status()}};
}
void BuildQueueEngine::loadFile(const std::string &path) {
    auto batch = readCsv(path);
    std::lock_guard<std::mutex> l(mutex_);
    if (jobs_.jobs().size() + batch.size() > 500)
        throw std::invalid_argument("Maximum 500 jobs");
    for (const auto &s : batch) {
        JobManager::validate(s, capacity_, mode_);
        if (s.arrival < clock_.now())
            throw std::invalid_argument("CSV arrival precedes current tick");
    }
    for (const auto &s : batch) {
        int id = jobs_.submit(s);
        event("JOB_SUBMITTED", id, "Loaded " + s.name + " from CSV.");
    }
    ++experimentRevision_;
    scenario_.clear();
    arrivalsLocked();
}
json BuildQueueEngine::demos() const {
    json result = json::array();
    for (const auto &name : {"fcfs", "sjf", "priority", "round_robin", "memory_first_fit",
                             "memory_best_fit", "memory_worst_fit", "fragmentation",
                             "memory_exhaustion", "synchronization", "mixed_workload"})
        result.push_back(name);
    return result;
}
void BuildQueueEngine::loadDemo(const std::string &requested) {
    std::string name = requested == "rr" ? "round_robin" : requested;
    auto names = demos();
    if (std::find(names.begin(), names.end(), name) == names.end())
        throw std::invalid_argument("Unknown demo");
    auto batch = readCsv(root_ + "/demos/" + name + ".csv");
    for (const auto &s : batch)
        JobManager::validate(s, 1024, "simulation");
    std::lock_guard<std::mutex> l(mutex_);
    resetLocked();
    scheduler_.policy("fcfs");
    configLocked({{"workers", 1}, {"slots", 2}, {"memory", 1024}, {"mode", "simulation"}});
    memory_.policy("first");
    quantum_ = 2;
    if (name == "sjf" || name == "priority")
        scheduler_.policy(name);
    if (name == "round_robin")
        scheduler_.policy("rr");
    if (name == "synchronization")
        configLocked({{"workers", 4}, {"slots", 2}});
    if (name == "mixed_workload") {
        configLocked({{"workers", 3}, {"slots", 2}});
        scheduler_.policy("rr");
    }
    if (name == "fragmentation" || name.find("memory_") == 0)
        configLocked({{"workers", 4}, {"slots", 4}});
    if (name == "memory_best_fit")
        memory_.policy("best");
    if (name == "memory_worst_fit")
        memory_.policy("worst");
    for (const auto &s : batch) {
        int id = jobs_.submit(s);
        event("JOB_SUBMITTED", id, "Loaded " + s.name + " from " + name + ".");
    }
    scenario_ = name;
    arrivalsLocked();
}
json BuildQueueEngine::compare() const {
    std::vector<JobSpec> specs;
    json cfg;
    std::uint64_t sourceRevision;
    {
        std::lock_guard<std::mutex> l(mutex_);
        cfg = configSnapshotLocked();
        sourceRevision = experimentRevision_;
        for (const auto &j : jobs_.jobs()) {
            if (j.spec.type != "simulated")
                throw std::invalid_argument("Comparison accepts simulated jobs only");
            specs.push_back(j.spec);
        }
    }
    if (specs.empty())
        throw std::invalid_argument("Load a workload before comparison");
    json results = json::array();
    for (const auto &p : {"fcfs", "sjf", "priority", "rr"}) {
        BuildQueueEngine e(root_, "");
        e.configure(
            {{"workers", cfg["workers"]}, {"slots", cfg["slots"]}, {"memory", cfg["memory"]}});
        e.memoryPolicy(cfg["memory_policy"]);
        e.policy(p, cfg["quantum"].get<int>());
        for (const auto &s : specs)
            e.submit(s);
        e.runUntilDone();
        auto state = e.snapshot();
        results.push_back({{"policy", p}, {"stats", state["stats"]}, {"gantt", state["gantt"]}});
    }
    return {{"configuration", cfg},
            {"source_revision", sourceRevision},
            {"workload_size", specs.size()},
            {"results", results}};
}
} // namespace bq
