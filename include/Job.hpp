#pragma once
#include "json.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace bq {
using json = nlohmann::json;
enum class JobState { NEW, READY, RUNNING, WAITING, COMPLETED, CANCELLED };
std::string stateName(JobState state);
struct JobSpec {
    std::string name = "Build task", type = "simulated";
    int arrival = 0, priority = 3, burst = 4, memory = 128;
};
struct Job {
    int id = 0;
    JobSpec spec;
    JobState state = JobState::NEW;
    int remaining = 0, firstRun = -1, completion = -1, readyWait = 0, resourceWait = 0;
    int worker = -1, memoryOffset = -1, quantumUsed = 0, dispatchSerial = 0;
    std::uint64_t readyOrder = 0;
    std::string waitReason, processError;
    int processExit = -1, processPid = -1;
    double processWallMs = 0;
    void transition(JobState next);
    json snapshot() const;
};
JobSpec parseJob(const json &input);
int integer(const json &input, const std::string &key, int fallback);
} // namespace bq
