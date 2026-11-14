#include "Test.hpp"
using namespace bq;
TEST(T1_Initialization) {
    BuildQueueEngine e(".", "");
    auto s = e.snapshot();
    CHECK(s["jobs"].empty());
    CHECK(s["stats"]["tick"] == 0);
    CHECK(s["config"]["workers"] == 1);
    CHECK(s["config"]["slots"] == 2);
    CHECK(s["memory"]["free"] == 1024);
}
TEST(T2_Submission_attributes_and_states) {
    BuildQueueEngine e(".", "");
    e.submit(test::job("now", 0, 3, 2, 64));
    e.submit(test::job("future", 4, 5, 1, 256));
    auto j = e.snapshot()["jobs"];
    CHECK(j[0]["state"] == "READY");
    CHECK(j[1]["state"] == "NEW");
    CHECK(j[1]["arrival"] == 4);
    CHECK(j[1]["burst"] == 5);
    CHECK(j[1]["priority"] == 1);
    CHECK(j[1]["memory"] == 256);
}
TEST(Invalid_input_atomicity_and_IDs) {
    BuildQueueEngine e(".", "");
    for (int n : {0, -1, 10001})
        test::throws([&] { e.submit(test::job("bad", 0, n)); });
    test::throws([&] { e.submit(test::job("bad", -1)); });
    test::throws([&] { e.submit(test::job("bad", 0, 1, 1, 1025)); });
    CHECK(e.snapshot()["jobs"].empty());
    CHECK(e.submit(test::job("valid")) == 1);
    e.reset();
    CHECK(e.submit(test::job("new ID")) == 2);
}
TEST(Cancellation_rules_and_repeated_cancellation) {
    BuildQueueEngine e(".", "");
    int a = e.submit(test::job("running"));
    int b = e.submit(test::job("queued"));
    e.step();
    test::throws([&] { e.cancel(a); });
    e.cancel(b);
    test::throws([&] { e.cancel(b); });
    test::throws([&] { e.cancel(999); });
    e.runUntilDone();
    test::throws([&] { e.cancel(a); });
    CHECK(e.snapshot()["stats"]["cancelled"] == 1);
}
TEST(Reset_and_safe_resize) {
    BuildQueueEngine e(".", "");
    e.submit(test::job("active"));
    e.step();
    test::throws([&] { e.configure({{"workers", 3}}); });
    e.reset();
    e.configure({{"workers", 3}, {"slots", 1}});
    auto s = e.snapshot();
    CHECK(s["jobs"].empty());
    CHECK(s["memory"]["free"] == 1024);
    CHECK(s["stats"]["tick"] == 0);
    CHECK(s["resources"]["active_slots"] == 0);
    CHECK(s["resources"]["shared_counter"] == 0);
}
TEST(Live_POSIX_process_reaped_and_bounded) {
    BuildQueueEngine e(BUILDQUEUE_ROOT, "");
    e.configure({{"mode", "live"}, {"workers", 2}});
    auto s = test::job("checksum", 0, 1);
    s.type = "process";
    e.submit(s);
    e.submit(s);
    e.runUntilDone();
    auto result = e.snapshot();
    for (const auto &j : result["jobs"]) {
        CHECK(j["process_exit"] == 0);
        CHECK(j["process_pid"].get<int>() > 0);
        CHECK(j["process_wall_ms"].get<double>() < 3000);
        CHECK(j["state"] == "COMPLETED");
    }
    test::throws([&] { e.policy("rr"); });
}
TEST(All_demos_complete_and_release_resources) {
    BuildQueueEngine listing(BUILDQUEUE_ROOT, "");
    auto demos = listing.demos();
    for (const auto &name : demos) {
        BuildQueueEngine e(BUILDQUEUE_ROOT, "");
        e.loadDemo(name);
        e.runUntilDone();
        auto s = e.snapshot();
        CHECK(s["stats"]["completed"] == s["stats"]["total"]);
        CHECK(s["memory"]["used"] == 0);
        CHECK(s["resources"]["active_slots"] == 0);
    }
}
TEST(Resize_preserves_historical_CPU_capacity) {
    BuildQueueEngine e(".", "");
    e.submit(test::job("one", 0, 1));
    e.step();
    e.configure({{"workers", 2}});
    e.submit(test::job("two", 1, 1));
    e.step();
    auto s = e.snapshot();
    CHECK(s["stats"]["worker_capacity_ticks"] == 3);
    NEAR(s["stats"]["cpu_utilization"].get<double>(), 200.0 / 3);
}
