#include "Test.hpp"
using namespace bq;
TEST(T3_FCFS_hand_calculated) {
    BuildQueueEngine e(".", "");
    e.submit(test::job("J1", 0, 4));
    e.submit(test::job("J2", 1, 3));
    e.submit(test::job("J3", 2, 2));
    e.runUntilDone();
    auto s = e.snapshot();
    auto jobs = s["jobs"];
    CHECK(test::sequence(s["gantt"]) == std::vector<int>({1, 2, 3}));
    CHECK(jobs[0]["completion"] == 4);
    CHECK(jobs[1]["completion"] == 7);
    CHECK(jobs[2]["completion"] == 9);
    CHECK(jobs[0]["ready_wait"] == 0);
    CHECK(jobs[1]["ready_wait"] == 3);
    CHECK(jobs[2]["ready_wait"] == 5);
    NEAR(s["stats"]["avg_wait"].get<double>(), 8.0 / 3);
    NEAR(s["stats"]["avg_turnaround"].get<double>(), 17.0 / 3);
    NEAR(s["stats"]["cpu_utilization"].get<double>(), 100.0);
    std::cout << "  Expected wait 8/3 = " << 8.0 / 3 << "; actual " << s["stats"]["avg_wait"]
              << " | Expected turnaround 17/3 = " << 17.0 / 3 << "; actual "
              << s["stats"]["avg_turnaround"] << "\n";
}
TEST(T4_SJF_nonpreemptive) {
    BuildQueueEngine e(".", "");
    e.policy("sjf");
    e.submit(test::job("long", 0, 5));
    e.submit(test::job("short", 1, 1));
    e.submit(test::job("medium", 1, 2));
    e.runUntilDone();
    auto s = e.snapshot();
    CHECK(test::sequence(s["gantt"]) == std::vector<int>({1, 2, 3}));
    CHECK(s["jobs"][0]["completion"] == 5);
    BuildQueueEngine all(".", "");
    all.policy("sjf");
    all.submit(test::job("long", 0, 5));
    all.submit(test::job("short", 0, 1));
    all.submit(test::job("medium", 0, 2));
    all.runUntilDone();
    CHECK(test::sequence(all.snapshot()["gantt"]) == std::vector<int>({2, 3, 1}));
}
TEST(T5_Priority_and_stable_ties) {
    BuildQueueEngine e(".", "");
    e.policy("priority");
    e.submit(test::job("low", 0, 2, 5));
    e.submit(test::job("high", 0, 2, 0));
    e.submit(test::job("tie", 0, 1, 0));
    e.runUntilDone();
    CHECK(test::sequence(e.snapshot()["gantt"]) == std::vector<int>({2, 3, 1}));
}
TEST(T6_RR_boundary_arrivals_and_remaining) {
    BuildQueueEngine e(".", "");
    e.policy("rr", 2);
    e.submit(test::job("A", 0, 5));
    e.submit(test::job("B", 0, 3));
    e.submit(test::job("C", 2, 1));
    e.step();
    e.step();
    auto at2 = e.snapshot();
    CHECK(at2["jobs"][0]["remaining"] == 3);
    CHECK(at2["jobs"][0]["state"] == "READY");
    CHECK(at2["resources"]["active_slots"] == 0);
    CHECK(at2["memory"]["used"] == 128);
    e.runUntilDone();
    auto s = e.snapshot();
    CHECK(test::sequence(s["gantt"]) == std::vector<int>({1, 2, 3, 1, 2, 1}));
    CHECK(s["stats"]["preemptions"] == 3);
    CHECK(s["jobs"][2]["first_run"] == 4);
}
TEST(Future_arrivals_and_explicit_idle) {
    BuildQueueEngine e(".", "");
    e.submit(test::job("future", 3, 2));
    e.runUntilDone();
    auto s = e.snapshot();
    CHECK(s["gantt"][0]["job"] == 0);
    CHECK(s["jobs"][0]["first_run"] == 3);
    CHECK(s["stats"]["tick"] == 5);
    NEAR(s["stats"]["cpu_utilization"].get<double>(), 40);
}
TEST(Deterministic_replay_and_conservation) {
    json first;
    for (int n = 0; n < 3; ++n) {
        BuildQueueEngine e(BUILDQUEUE_ROOT, "");
        e.loadDemo("mixed_workload");
        e.runUntilDone();
        auto s = e.snapshot();
        s.erase("log");
        if (n == 0)
            first = s;
        else
            CHECK(first == s);
        for (const auto &j : s["jobs"]) {
            CHECK(j["turnaround"].get<int>() == j["burst"].get<int>() + j["ready_wait"].get<int>() +
                                                    j["resource_wait"].get<int>());
        }
    }
}
TEST(Policy_switch_preserves_running_job) {
    BuildQueueEngine e(".", "");
    e.submit(test::job("A", 0, 5));
    e.submit(test::job("B", 0, 2));
    e.step();
    e.policy("rr", 2);
    e.step();
    CHECK(e.snapshot()["jobs"][0]["remaining"] == 3);
    e.step();
    CHECK(e.snapshot()["stats"]["preemptions"] == 1);
    e.runUntilDone();
    CHECK(e.snapshot()["stats"]["completed"] == 2);
}
TEST(Comparison_isolated_identical_inputs) {
    BuildQueueEngine e(BUILDQUEUE_ROOT, "");
    e.loadDemo("sjf");
    auto before = e.snapshot();
    auto c = e.compare();
    CHECK(c["results"].size() == 4);
    CHECK(before == e.snapshot());
    for (const auto &row : c["results"])
        CHECK(row["stats"]["completed"] == 4);
}
TEST(RR_slice_boundaries_survive_log_clearing) {
    BuildQueueEngine e(".", "");
    e.policy("rr", 1);
    e.submit(test::job("only", 0, 3));
    e.step();
    e.clearLog();
    e.runUntilDone();
    auto s = e.snapshot();
    CHECK(s["gantt"][0]["dispatch"] != s["gantt"][1]["dispatch"]);
    CHECK(s["gantt"][1]["dispatch"] != s["gantt"][2]["dispatch"]);
}
