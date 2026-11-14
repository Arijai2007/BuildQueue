#include "Scenario.hpp"
#include "Test.hpp"
#include <atomic>
#include <fstream>
using namespace bq;
TEST(Concurrent_memory_requests_preserve_partition) {
    MemoryManager memory(4096);
    std::atomic<bool> invalid{false};
    std::vector<std::thread> workers;
    for (int id = 1; id <= 8; ++id)
        workers.emplace_back([&, id] {
            for (int n = 0; n < 100; ++n) {
                if (memory.allocate(id, 32) < 0)
                    invalid = true;
                auto s = memory.snapshot();
                int offset = 0;
                for (const auto &b : s["blocks"]) {
                    if (b["offset"] != offset)
                        invalid = true;
                    offset += b["size"].get<int>();
                }
                if (offset != 4096)
                    invalid = true;
                if (!memory.release(id))
                    invalid = true;
            }
        });
    for (auto &worker : workers)
        worker.join();
    CHECK(!invalid);
    CHECK(memory.snapshot()["free"] == 4096);
    CHECK(memory.snapshot()["blocks"].size() == 1);
}
TEST(Adversarial_RR_owners_progress_under_memory_pressure) {
    BuildQueueEngine e(".", "");
    e.configure({{"workers", 3}, {"slots", 2}, {"memory", 256}});
    e.policy("rr", 1);
    for (int i = 0; i < 8; ++i)
        e.submit(test::job("pressure", 0, 3, 1, 128));
    e.runUntilDone();
    auto s = e.snapshot();
    CHECK(s["stats"]["completed"] == 8);
    CHECK(s["resources"]["shared_counter"] == 24);
    CHECK(s["memory"]["free"] == 256);
}
TEST(Process_errors_are_visible_and_resources_release) {
    BuildQueueEngine e("/nonexistent-buildqueue-fixture", "");
    e.configure({{"mode", "live"}});
    auto j = test::job("failed checksum", 0, 1);
    j.type = "process";
    e.submit(j);
    e.runUntilDone();
    auto s = e.snapshot();
    CHECK(s["jobs"][0]["process_exit"].get<int>() != 0);
    CHECK(!s["jobs"][0]["process_error"].get<std::string>().empty());
    CHECK(s["memory"]["used"] == 0);
    CHECK(s["resources"]["active_slots"] == 0);
}
TEST(State_machine_rejects_invalid_transitions) {
    Job j;
    test::throws([&] { j.transition(JobState::RUNNING); });
    j.transition(JobState::READY);
    j.transition(JobState::RUNNING);
    test::throws([&] { j.transition(JobState::CANCELLED); });
    j.transition(JobState::COMPLETED);
    test::throws([&] { j.transition(JobState::READY); });
}
TEST(Invalid_configuration_is_transactional) {
    BuildQueueEngine e(".", "");
    auto before = e.snapshot();
    test::throws([&] { e.configure({{"workers", 4}, {"slots", 0}}); });
    CHECK(before == e.snapshot());
    test::throws([&] { e.policy("rr", 0); });
    CHECK(before == e.snapshot());
}
