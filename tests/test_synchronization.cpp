#include "Test.hpp"
#include <atomic>
using namespace bq;
TEST(T8_Concurrent_semaphore_and_protected_counter) {
    Semaphore semaphore(2);
    std::mutex mutex;
    int active = 0, peak = 0, counter = 0;
    std::vector<std::thread> workers;
    for (int w = 0; w < 8; ++w)
        workers.emplace_back([&] {
            for (int k = 0; k < 200; ++k) {
                semaphore.acquire();
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++active;
                    peak = std::max(peak, active);
                }
                std::this_thread::yield();
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++counter;
                    --active;
                }
                semaphore.release();
            }
        });
    for (auto &w : workers)
        w.join();
    CHECK(counter == 1600);
    CHECK(active == 0);
    CHECK(peak <= 2);
}
TEST(Worker_queue_concurrency_and_clean_shutdown) {
    std::atomic<int> counter{0};
    std::vector<std::future<WorkResult>> futures;
    {
        WorkerManager pool(4);
        for (int n = 0; n < 100; ++n)
            futures.push_back(pool.submit([&] {
                counter.fetch_add(1);
                return WorkResult{};
            }));
        for (auto &f : futures)
            f.get();
        pool.resize(2);
    }
    CHECK(counter == 100);
}
TEST(Resource_contention_and_multiworker_lanes) {
    BuildQueueEngine e(BUILDQUEUE_ROOT, "");
    e.loadDemo("synchronization");
    e.step();
    auto s = e.snapshot();
    CHECK(s["resources"]["active_slots"] == 2);
    CHECK(s["stats"]["waiting"] == 2);
    CHECK(s["gantt"].size() == 4);
    e.runUntilDone();
    s = e.snapshot();
    CHECK(s["resources"]["peak_slots"] == 2);
    CHECK(s["resources"]["shared_counter"] == 18);
    CHECK(s["stats"]["busy_worker_ticks"] == 18);
    CHECK(s["stats"]["contention_events"].get<int>() > 0);
}
TEST(Concurrent_snapshots_while_coordinator_runs) {
    BuildQueueEngine e(".", "");
    e.configure({{"workers", 4}, {"slots", 2}, {"delay_ms", 1}});
    for (int n = 0; n < 20; ++n)
        e.submit(test::job("task", 0, 3));
    std::atomic<bool> invalid{false};
    e.run();
    std::vector<std::thread> readers;
    for (int n = 0; n < 4; ++n)
        readers.emplace_back([&] {
            for (int i = 0; i < 30; ++i) {
                auto s = e.snapshot();
                if (s["resources"]["active_slots"].get<int>() > 2)
                    invalid = true;
            }
        });
    for (auto &t : readers)
        t.join();
    e.pause();
    e.runUntilDone();
    CHECK(!invalid);
    CHECK(e.snapshot()["stats"]["completed"] == 20);
}
