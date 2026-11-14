#pragma once
#include "JobManager.hpp"
#include "Logger.hpp"
#include "MemoryManager.hpp"
#include "MonitoringEngine.hpp"
#include "SchedulerEngine.hpp"
#include "SimulationClock.hpp"
#include "SyncController.hpp"
#include "WorkerManager.hpp"
#include <condition_variable>
namespace bq {
class BuildQueueEngine {
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    JobManager jobs_;
    SchedulerEngine scheduler_;
    MemoryManager memory_;
    SyncController sync_;
    std::unique_ptr<WorkerManager> pool_ = std::make_unique<WorkerManager>(1);
    SimulationClock clock_;
    Logger logger_;
    std::thread coordinator_;
    bool stopping_ = false, running_ = false;
    int workers_ = 1, slots_ = 2, capacity_ = 1024, quantum_ = 2, delayMs_ = 350;
    int busy_ = 0, workerCapacityTicks_ = 0, preemptions_ = 0, dispatches_ = 0, contention_ = 0,
        peakMemory_ = 0;
    std::uint64_t readySequence_ = 0;
    std::uint64_t experimentRevision_ = 0;
    std::string scenario_;
    std::string mode_ = "simulation", root_,
                explanation_ = "Submit jobs or load a demonstration to begin.";
    std::vector<int> lanes_{0};
    json gantt_ = json::array();
    void coordinate();
    bool stepLocked();
    bool unfinishedLocked() const;
    void arrivalsLocked();
    void resetLocked();
    void configLocked(const json &);
    void readyLocked(Job &, const std::string &event = "JOB_READY");
    void releaseLocked(Job &);
    void event(const std::string &, int, const std::string &);
    json configSnapshotLocked() const;

  public:
    explicit BuildQueueEngine(std::string root = ".", std::string logFile = "logs/buildqueue.log");
    ~BuildQueueEngine();
    BuildQueueEngine(const BuildQueueEngine &) = delete;
    int submit(const JobSpec &, bool arrivalNow = false);
    void cancel(int id);
    void policy(const std::string &, int quantum = 2);
    void memoryPolicy(const std::string &);
    void configure(const json &);
    bool step();
    void run();
    void pause();
    void runUntilDone(int maxTicks = 500000);
    void reset();
    void clearLog();
    void loadFile(const std::string &);
    void loadDemo(const std::string &);
    json compare() const;
    json snapshot() const;
    json demos() const;
};
} // namespace bq
