#include "DeadlockDetector.hpp"

#include <atomic>
#include <iostream>

int main() {
    bq::DeadlockDetector detector;
    bq::TrackedMutex first(detector, "first resource");
    bq::TrackedMutex second(detector, "second resource");
    int ready = 0;
    std::mutex barrier;
    std::condition_variable bothReady;
    std::atomic<int> completed{0};
    std::atomic<int> rejected{0};

    auto worker = [&](bq::TrackedMutex& owned, bq::TrackedMutex& requested) {
        try {
            std::lock_guard<bq::TrackedMutex> hold(owned);
            {
                std::unique_lock<std::mutex> lock(barrier);
                ++ready;
                bothReady.notify_all();
                bothReady.wait(lock, [&] { return ready == 2; });
            }
            std::lock_guard<bq::TrackedMutex> acquire(requested);
            ++completed;
        } catch (const bq::DeadlockDetected&) {
            ++rejected;
        }
    };

    std::thread a([&] { worker(first, second); });
    std::thread b([&] { worker(second, first); });
    a.join();
    b.join();

    const auto report = detector.report();
    if (completed != 1 || rejected != 1 || report.count != 1 ||
        report.lastCycle.find("first resource") == std::string::npos ||
        report.lastCycle.find("second resource") == std::string::npos) {
        std::cerr << "FAIL: expected one confirmed cycle and one successful worker\n";
        return 1;
    }

    {
        std::lock_guard<bq::TrackedMutex> holdFirst(first);
        std::lock_guard<bq::TrackedMutex> holdSecond(second);
    }
    if (detector.report().count != 1) {
        std::cerr << "FAIL: uncontended locks should remain usable without a new alert\n";
        return 1;
    }
    std::cout << "Detected cycle: " << report.lastCycle << '\n'
              << "PASS: one request was rejected and the other worker finished\n";
}
