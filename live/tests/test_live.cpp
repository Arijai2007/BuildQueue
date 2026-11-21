#include "BatchQueue.hpp"

#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
using namespace bq;

void require(bool condition, const char* problem) {
    if (!condition)
        throw std::runtime_error(problem);
}

struct Fixture {
    std::filesystem::path path;
    Fixture() {
        char name[] = "/tmp/buildqueue-test-XXXXXX";
        auto directory = ::mkdtemp(name);
        require(directory != nullptr, "Could not create fixture directory");
        path = directory;
    }
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void checksumProcess() {
    Fixture fixture;
    std::ofstream(fixture.path / "empty file");
    std::filesystem::create_directory(fixture.path / "folder");
    std::filesystem::create_symlink(fixture.path / "empty file", fixture.path / "alias");
    Checksum checksum(fixture.path);
    const auto listed = checksum.files();
    require(listed.size() == 1 && listed.front().name == "empty file",
            "Catalog must include only regular files.");
    const auto result = checksum.run("empty file");
    require(checksum.size("empty file") == 0 && Checksum::pathSize(fixture.path / "empty file") == 0,
            "Empty files should have a known zero-byte estimate.");
    require(!checksum.size("alias") && !Checksum::pathSize(fixture.path / "alias"),
            "Symlinks should have no size estimate.");
    require(result.pid > 0 && result.exitCode == 0 && result.error.empty(),
            "Expected a real successful checksum process.");
    require(result.checksum == 4294967295U && result.bytes == 0, "Incorrect empty-file checksum.");
    require(!checksum.run("missing").error.empty(), "Missing file should fail.");
    require(!checksum.run("alias").error.empty(), "Symbolic link should fail.");
    require(!checksum.run("../file").error.empty(), "Relative path should fail.");
    require(!checksum.run("folder").error.empty(), "Directory should fail.");
    require(!Checksum::runPath("empty file").error.empty(), "Desktop requires an absolute path.");
    require(!Checksum::runPath(fixture.path / "alias").error.empty(), "Desktop rejects symlinks.");
    BatchQueue desktop(2, 1, [](const std::string& name) { return Checksum::runPath(name); },
                       Input::AbsolutePaths);
    desktop.submit({(fixture.path / "empty file").string()}, 0);
    desktop.waitUntilIdle();
    const auto snapshot = desktop.snapshot();
    require(snapshot.jobs.front().state == State::Completed &&
            snapshot.jobs.front().result.checksum == result.checksum,
            "Desktop should run a real checksum on an absolute file path.");
}

void scheduling(Policy policy) {
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    std::vector<std::string> started;
    BatchQueue queue(4, 1, [&](const std::string& name) {
        std::unique_lock<std::mutex> lock(mutex);
        started.push_back(name);
        changed.notify_all();
        if (name == "first")
            changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; });
        ChecksumResult result;
        result.exitCode = 0;
        return result;
    }, Input::FolderNames, [](const std::string& name) -> std::optional<std::uintmax_t> {
        if (name == "unknown")
            return std::nullopt;
        return name == "high" ? 0 : 20;
    });
    require(queue.snapshot().policy == Policy::Fcfs, "FCFS should be the default.");
    queue.setPolicy(policy);
    queue.submit({"first"}, 0);
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return !started.empty(); }),
                "Worker did not start.");
    }
    queue.submit(std::vector<JobRequest>{{"low", 9}, {"unknown", 9}, {"high", 1}, {"high-tie", 1}});
    const auto cancelled = queue.submit({"cancel"}, 0);
    const bool didCancel = queue.cancel(cancelled.front());
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
    }
    changed.notify_all();
    queue.waitUntilIdle();
    std::vector<std::string> expected;
    switch (policy) {
        case Policy::Fcfs: expected = {"first", "low", "unknown", "high", "high-tie"}; break;
        case Policy::Priority: expected = {"first", "high", "high-tie", "low", "unknown"}; break;
        case Policy::Sjf: expected = {"first", "high", "low", "high-tie", "unknown"}; break;
    }
    require(didCancel, "Queued job should be cancellable.");
    require(started == expected, "Jobs ran in the wrong order.");
    const auto snapshot = queue.snapshot();
    for (const auto& job : snapshot.jobs) {
        if (job.state == State::Cancelled) {
            require(job.dispatchOrder == 0, "Cancelled jobs should have no dispatch order.");
            continue;
        }
        require(job.dispatchOrder > 0 && job.dispatchOrder <= expected.size(),
                "Completed job should have a dispatch order.");
        require(expected[job.dispatchOrder - 1] == job.file && job.dispatchPolicy == policy,
                "Dispatch metadata should match the measured run.");
    }
    require(queue.snapshot().peak == 1, "Exceeded the concurrency limit.");
    queue.clearFinished();
    require(queue.snapshot().jobs.empty(), "Clearing terminal jobs should empty history.");
}

void concurrency() {
    std::mutex mutex;
    std::condition_variable changed;
    int started = 0;
    bool release = false;
    BatchQueue queue(4, 2, [&](const std::string&) {
        std::unique_lock<std::mutex> lock(mutex);
        ++started;
        changed.notify_all();
        changed.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
        ChecksumResult result;
        result.exitCode = 0;
        return result;
    });
    queue.submit({"one", "two", "three", "four"}, 0);
    bool reached;
    Snapshot active;
    {
        std::unique_lock<std::mutex> lock(mutex);
        reached = changed.wait_for(lock, std::chrono::seconds(5), [&] { return started >= 2; });
        active = queue.snapshot();
        release = true;
    }
    changed.notify_all();
    queue.waitUntilIdle();
    require(reached && active.running == 2, "Two jobs should run concurrently.");
    require(queue.snapshot().peak == 2 && started == 4, "Slot limit or queue progress is incorrect.");
}
}

int main() {
    try {
        checksumProcess();
        scheduling(bq::Policy::Fcfs);
        scheduling(bq::Policy::Priority);
        scheduling(bq::Policy::Sjf);
        concurrency();
        std::cout << "PASS: real checksum process, FCFS, Priority, SJF, cancellation and concurrency\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
