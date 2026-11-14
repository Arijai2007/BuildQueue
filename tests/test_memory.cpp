#include "Test.hpp"
using namespace bq;
TEST(Resource_wait_does_not_become_CPU_wait_while_lanes_are_busy) {
    BuildQueueEngine e(".", "");
    e.configure({{"workers", 2}, {"slots", 2}});
    e.submit(test::job("A", 0, 4, 1, 256));
    e.submit(test::job("blocked", 0, 1, 1, 900));
    e.submit(test::job("C", 0, 3, 1, 128));
    e.step();
    e.step();
    auto s = e.snapshot();
    CHECK(s["jobs"][1]["state"] == "WAITING");
    CHECK(s["jobs"][1]["resource_wait"] == 2);
    CHECK(s["jobs"][1]["ready_wait"] == 0);
    e.runUntilDone();
    s = e.snapshot();
    CHECK(s["jobs"][1]["resource_wait"] == 4);
    CHECK(s["jobs"][1]["first_run"] == 4);
}
TEST(T7_Memory_all_policies_fragmented_addresses) {
    for (const auto &p : {"first", "best", "worst"}) {
        MemoryManager m(1024);
        CHECK(m.allocate(1, 180) == 0);
        CHECK(m.allocate(2, 100) == 180);
        CHECK(m.allocate(3, 100) == 280);
        CHECK(m.allocate(4, 344) == 380);
        CHECK(m.release(1));
        CHECK(m.release(3));
        m.policy(p);
        int expected = std::string(p) == "best" ? 280 : std::string(p) == "worst" ? 724 : 0;
        CHECK(m.allocate(5, 90) == expected);
    }
}
TEST(T9_Insufficient_memory_waits_then_resumes) {
    BuildQueueEngine e(".", "");
    e.configure({{"workers", 2}});
    e.submit(test::job("A", 0, 3, 1, 768));
    e.submit(test::job("B", 0, 2, 1, 512));
    e.step();
    auto s = e.snapshot();
    CHECK(s["jobs"][1]["state"] == "WAITING");
    CHECK(s["jobs"][1]["wait_reason"] == "MEMORY_UNAVAILABLE");
    CHECK(s["jobs"][1]["remaining"] == 2);
    e.runUntilDone();
    s = e.snapshot();
    CHECK(s["stats"]["completed"] == 2);
    CHECK(s["jobs"][1]["resource_wait"] == 3);
    CHECK(s["memory"]["free"] == 1024);
}
TEST(T10_Resource_release_and_cancel_retained_memory) {
    BuildQueueEngine e(".", "");
    e.policy("rr", 1);
    int id = e.submit(test::job("cancel", 0, 3));
    e.step();
    CHECK(e.snapshot()["memory"]["used"] == 128);
    e.cancel(id);
    CHECK(e.snapshot()["memory"]["used"] == 0);
    e.submit(test::job("reuse", 1, 1, 1, 1024));
    e.runUntilDone();
    CHECK(e.snapshot()["memory"]["free_blocks"] == 1);
    CHECK(e.snapshot()["resources"]["active_slots"] == 0);
}
TEST(Coalescing_double_free_and_ownership) {
    MemoryManager m(100);
    m.allocate(1, 30);
    m.allocate(2, 40);
    m.allocate(3, 30);
    CHECK(m.release(2));
    CHECK(!m.release(2));
    CHECK(!m.release(99));
    test::throws([&] { m.allocate(1, 1); });
    m.release(1);
    m.release(3);
    CHECK(m.snapshot()["blocks"].size() == 1);
    CHECK(m.snapshot()["largest_free"] == 100);
    CHECK(m.snapshot()["internal_fragmentation"] == 0);
}
TEST(Observable_fragmentation_demo) {
    BuildQueueEngine e(BUILDQUEUE_ROOT, "");
    e.loadDemo("fragmentation");
    e.step();
    e.step();
    auto s = e.snapshot();
    CHECK(s["memory"]["free"] == 580);
    CHECK(s["memory"]["largest_free"] == 300);
    e.step();
    s = e.snapshot();
    CHECK(s["jobs"][4]["state"] == "WAITING");
    CHECK(s["memory"]["free"].get<int>() > 400);
    CHECK(s["memory"]["largest_free"].get<int>() < 400);
    e.runUntilDone();
    CHECK(e.snapshot()["stats"]["completed"] == 5);
}
TEST(Memory_policy_demos_match_selected_address) {
    for (const auto &p : {"first", "best", "worst"}) {
        BuildQueueEngine e(BUILDQUEUE_ROOT, "");
        e.loadDemo("memory_" + std::string(p) + "_fit");
        e.step();
        e.step();
        e.step();
        int expected = std::string(p) == "best" ? 280 : std::string(p) == "worst" ? 724 : 0;
        CHECK(e.snapshot()["jobs"][4]["memory_offset"] == expected);
    }
}
