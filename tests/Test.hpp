#pragma once
#include "BuildQueueEngine.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
namespace test {
struct Case {
    std::string name;
    std::function<void()> run;
};
std::vector<Case> &cases();
struct Register {
    Register(std::string n, std::function<void()> f) {
        cases().push_back({std::move(n), std::move(f)});
    }
};
inline void check(bool ok, const char *expression, int line) {
    if (!ok)
        throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
template <class F> void throws(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, "expected rejection", __LINE__);
}
inline bq::JobSpec job(std::string name, int arrival = 0, int burst = 4, int priority = 3,
                       int memory = 128) {
    bq::JobSpec s;
    s.name = std::move(name);
    s.arrival = arrival;
    s.burst = burst;
    s.priority = priority;
    s.memory = memory;
    return s;
}
inline std::vector<int> sequence(const bq::json &gantt) {
    std::vector<int> r;
    int last = -1;
    for (const auto &e : gantt) {
        if (e["worker"] != 0)
            continue;
        int id = e["job"];
        if (id != last) {
            r.push_back(id);
            last = id;
        }
    }
    return r;
}
} // namespace test
#define TEST(name)                                                                                 \
    static void name();                                                                            \
    static test::Register reg_##name(#name, name);                                                 \
    static void name()
#define CHECK(expr) test::check(static_cast<bool>(expr), #expr, __LINE__)
#define NEAR(a, b) CHECK(std::abs((a) - (b)) < 1e-10)
