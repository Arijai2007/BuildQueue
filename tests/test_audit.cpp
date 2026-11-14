#include "Api.hpp"
#include "Test.hpp"
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace bq;

TEST(Audit_exact_tick_budget_finishes_without_false_error) {
    BuildQueueEngine engine(".", "");
    engine.submit(test::job("one tick", 0, 1));
    engine.runUntilDone(1);
    CHECK(engine.snapshot()["stats"]["completed"] == 1);
}

TEST(Audit_documented_maximum_memory_capacity_is_accepted) {
    BuildQueueEngine engine(".", "");
    engine.configure({{"memory", 1048576}});
    CHECK(engine.snapshot()["memory"]["total"] == 1048576);
    engine.submit(test::job("full capacity", 0, 1, 1, 1048576));
    engine.runUntilDone();
    CHECK(engine.snapshot()["memory"]["free"] == 1048576);
}

TEST(Audit_missing_and_malformed_API_identifiers_are_bad_requests) {
    BuildQueueEngine engine(".", "");
    Api api(engine);
    CHECK(api.request("POST", "/api/cancel", "{}").status == 400);
    CHECK(api.request("GET", "/api/jobs/99999999999999999999999999999").status == 400);
    CHECK(api.request("GET", "/api/jobs/+1").status == 400);
    CHECK(api.request("POST", "/api/workers", "{}").status == 400);
}
TEST(Audit_arrival_now_is_assigned_atomically_at_submission) {
    BuildQueueEngine engine(".", "");
    engine.submit(test::job("first", 0, 2));
    engine.runUntilDone();
    Api api(engine);
    CHECK(api.request("POST", "/api/submit", R"({"name":"now","burst":1,"arrival_now":true})")
              .status == 201);
    auto state = engine.snapshot();
    CHECK(state["jobs"][1]["arrival"] == 2);
    CHECK(state["jobs"][1]["state"] == "READY");
    CHECK(api.request("POST", "/api/submit", R"({"arrival_now":"true"})").status == 400);
}

TEST(Audit_revisions_track_inputs_without_invalidating_tick_playback) {
    BuildQueueEngine engine(BUILDQUEUE_ROOT, "");
    engine.loadDemo("sjf");
    auto revision = engine.snapshot()["experiment_revision"];
    CHECK(engine.snapshot()["scenario"] == "sjf");
    engine.step();
    CHECK(engine.snapshot()["experiment_revision"] == revision);
    auto comparison = engine.compare();
    CHECK(comparison["source_revision"] == revision);
    engine.configure({{"delay_ms", 100}});
    CHECK(engine.snapshot()["experiment_revision"] == revision);
    engine.policy("priority");
    CHECK(engine.snapshot()["experiment_revision"] != revision);
    engine.reset();
    CHECK(engine.snapshot()["scenario"] == "");
}

TEST(Audit_repeated_reset_reclaims_resources_and_keeps_unique_IDs) {
    BuildQueueEngine engine(BUILDQUEUE_ROOT, "");
    int lastID = 0;
    for (int i = 0; i < 15; ++i) {
        engine.loadDemo(i % 2 ? "synchronization" : "mixed_workload");
        engine.step();
        engine.step();
        auto snapshot = engine.snapshot();
        CHECK(snapshot["jobs"][0]["id"].get<int>() > lastID);
        lastID = snapshot["jobs"].back()["id"];
        engine.reset();
        snapshot = engine.snapshot();
        CHECK(snapshot["jobs"].empty());
        CHECK(snapshot["memory"]["free"] == 1024);
        CHECK(snapshot["resources"]["active_slots"] == 0);
        CHECK(snapshot["resources"]["shared_counter"] == 0);
    }
}

TEST(Audit_process_timeout_is_reported_and_child_is_reaped) {
    struct Fixture {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     ("buildqueue-timeout-" + std::to_string(::getpid()));
        Fixture() {
            std::filesystem::create_directories(root / "demos");
        }
        ~Fixture() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } fixture;
    const auto input = (fixture.root / "demos/process_input.txt").string();
    CHECK(::mkfifo(input.c_str(), 0600) == 0);
    BuildQueueEngine engine(fixture.root.string(), "");
    engine.configure({{"mode", "live"}});
    auto spec = test::job("bounded checksum", 0, 1);
    spec.type = "process";
    engine.submit(spec);
    engine.runUntilDone();
    auto snapshot = engine.snapshot();
    auto job = snapshot["jobs"][0];
    CHECK(job["process_exit"] == 124);
    CHECK(job["process_wall_ms"].get<double>() >= 3000);
    CHECK(job["process_wall_ms"].get<double>() < 6000);
    int status = 0;
    errno = 0;
    CHECK(::waitpid(job["process_pid"].get<int>(), &status, WNOHANG) == -1);
    CHECK(errno == ECHILD);
    CHECK(snapshot["memory"]["used"] == 0);
    CHECK(snapshot["resources"]["active_slots"] == 0);
}

#ifdef __linux__
TEST(Audit_disk_write_failure_cannot_leak_a_build_slot) {
    BuildQueueEngine engine(".", "/dev/full");
    // These catches reproduce the original disk-full failure at several tick phases.
    try {
        engine.submit(test::job("disk full", 0, 2));
    } catch (const std::exception &) {
    }
    for (int i = 0; i < 3; ++i) {
        try {
            engine.step();
        } catch (const std::exception &) {
        }
    }
    try {
        engine.reset();
    } catch (const std::exception &) {
    }
    auto state = engine.snapshot();
    CHECK(state["resources"]["active_slots"] == 0);
    CHECK(state["memory"]["used"] == 0);
    CHECK(!state["logging"]["healthy"].get<bool>());
    CHECK(!state["logging"]["error"].get<std::string>().empty());
}
#endif
