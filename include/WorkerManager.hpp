#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
namespace bq {
struct WorkResult {
    int exitCode = 0, pid = -1;
    double wallMs = 0;
    std::string error;
};
class WorkerManager {
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> threads_;
    bool stopping_ = false;
    void shutdown();

  public:
    explicit WorkerManager(int count = 1);
    ~WorkerManager();
    void resize(int count);
    std::future<WorkResult> submit(std::function<WorkResult()> work);
    static WorkResult execute(bool process, bool live, const std::string &inputPath);
};
} // namespace bq
