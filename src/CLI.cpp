#include "CLI.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
namespace bq {
namespace {
std::vector<std::string> tokenize(const std::string &s) {
    std::istringstream in(s);
    std::vector<std::string> result;
    std::string word;
    while (in >> std::quoted(word))
        result.push_back(word);
    return result;
}
int number(const std::string &s) {
    std::size_t n = 0;
    int v = std::stoi(s, &n);
    if (n != s.size())
        throw std::invalid_argument("Expected integer: " + s);
    return v;
}
} // namespace
void CLI::banner() {
    bool color = ::isatty(STDOUT_FILENO);
    if (color)
        out_ << "\033[38;5;105m";
    out_ << "\n  ▰ ▰  B U I L D Q U E U E\n  An Intelligent Local Job & Resource Management "
            "System\n";
    if (color)
        out_ << "\033[0m";
    out_ << "  Jaypee Institute of Information Technology\n  Operating Systems Lab · 24B15CS312 · "
            "Project Based Learning (PBL)\n"
         << "  Arihant Jain (992401030014, F1) · Yuvraj Rana (992401030021, F1)\n  Siddharth Singh "
            "(992401030026, F1)\n  Submitted to Dr. Ambalika Sarkar\n\n";
}
void CLI::help() {
    out_ << "submit \"name\" --burst N --prio N --mem N [--arrival N] [--type simulated|process]\n"
            "list | status [id] | cancel id | policy fcfs|sjf|priority|rr [quantum]\n"
            "mem-policy first|best|worst | memmap | workers N | slots N\n"
            "mode simulation|live | delay milliseconds | run | pause | resume | step | finish\n"
            "reset | report | gantt | log | load path.csv | demo scenario | compare | help | exit\n"
            "run/resume starts asynchronously; finish runs synchronously to completion.\n"
            "One-shot: --demo fcfs|rr --load path.csv --serve --port 8080 --help\n"
            "Options: --policy NAME --quantum N --workers N --slots N --mode NAME\n"
            "         --mem-policy NAME --delay N --interactive --json\n";
}
void CLI::report() {
    auto s = engine_.snapshot();
    auto m = s["stats"];
    out_ << std::fixed << std::setprecision(3) << "\nTick " << m["tick"] << " | completed "
         << m["completed"] << "/" << m["total"] << " | cancelled " << m["cancelled"] << "\n"
         << "Average READY wait: " << m["avg_wait"]
         << " | Resource wait: " << m["avg_resource_wait"] << "\n"
         << "Average turnaround: " << m["avg_turnaround"] << " | Response: " << m["avg_response"]
         << "\n"
         << "CPU utilization: " << m["cpu_utilization"] << "% | Throughput: " << m["throughput"]
         << " jobs/tick\n"
         << "Preemptions: " << m["preemptions"] << " | Dispatches: " << m["dispatches"]
         << " | Contention: " << m["contention_events"] << "\n"
         << "Memory used: " << s["memory"]["used"] << "/" << s["memory"]["total"]
         << " MB | Peak: " << m["peak_memory"] << " MB\n"
         << "Averages include completed jobs only; memory reservations are simulated.\n";
}
void CLI::gantt() {
    auto s = engine_.snapshot();
    int laneCount = s["config"]["workers"];
    for (const auto &e : s["gantt"])
        laneCount = std::max(laneCount, e["worker"].get<int>() + 1);
    for (int w = 0; w < laneCount; ++w) {
        out_ << "W" << w + 1 << ": ";
        int last = -1, start = 0, end = 0, lastDispatch = -1;
        auto flush = [&] {
            if (last >= 0)
                out_ << (last ? "J" + std::to_string(last) : "IDLE") << " [" << start << "–" << end
                     << "] ";
        };
        for (const auto &e : s["gantt"])
            if (e["worker"] == w) {
                int id = e["job"], a = e["start"], b = e["end"], dispatch = e["dispatch"];
                if (id == last && a == end && dispatch == lastDispatch)
                    end = b;
                else {
                    flush();
                    last = id;
                    start = a;
                    end = b;
                    lastDispatch = dispatch;
                }
            }
        flush();
        out_ << "\n";
    }
}
bool CLI::command(const std::string &line) {
    auto t = tokenize(line);
    if (t.empty())
        return true;
    auto c = t[0];
    auto arg = [&](std::size_t i) -> std::string {
        if (i >= t.size())
            throw std::invalid_argument("Missing argument; use help");
        return t[i];
    };
    auto count = [&](std::size_t n) {
        if (t.size() != n)
            throw std::invalid_argument("Unexpected or missing arguments; use help");
    };
    if (c == "exit" || c == "quit") {
        count(1);
        engine_.pause();
        return false;
    }
    if (c == "help") {
        count(1);
        help();
    } else if (c == "submit") {
        JobSpec s;
        bool arrivalNow = true;
        s.name = arg(1);
        s.arrival = engine_.snapshot()["stats"]["tick"];
        if (t.size() % 2 != 0)
            throw std::invalid_argument("Submit options require values");
        for (std::size_t i = 2; i < t.size(); i += 2) {
            auto key = t[i], v = arg(i + 1);
            if (key == "--burst")
                s.burst = number(v);
            else if (key == "--prio")
                s.priority = number(v);
            else if (key == "--mem")
                s.memory = number(v);
            else if (key == "--arrival") {
                s.arrival = number(v);
                arrivalNow = false;
            } else if (key == "--type")
                s.type = v;
            else
                throw std::invalid_argument("Unknown option " + key);
        }
        out_ << "Submitted J" << engine_.submit(s, arrivalNow) << "\n";
    } else if (c == "list" || (c == "status" && t.size() == 1)) {
        count(1);
        auto s = engine_.snapshot();
        out_ << "ID   NAME                      ARR  BURST REM  PRIO MEM   STATE       WORKER\n";
        for (const auto &j : s["jobs"])
            out_ << std::left << std::setw(5) << j["id"].get<int>() << std::setw(26)
                 << j["name"].get<std::string>().substr(0, 24) << std::setw(5)
                 << j["arrival"].get<int>() << std::setw(6) << j["burst"].get<int>() << std::setw(5)
                 << j["remaining"].get<int>() << std::setw(5) << j["priority"].get<int>()
                 << std::setw(6) << j["memory"].get<int>() << std::setw(12)
                 << j["state"].get<std::string>() << j["worker"] << "\n";
        if (s["jobs"].empty())
            out_ << "Queue empty. Use demo fcfs or submit.\n";
    } else if (c == "status") {
        count(2);
        int id = number(arg(1));
        bool found = false;
        auto snapshot = engine_.snapshot();
        for (const auto &j : snapshot["jobs"]) {
            if (j["id"] == id) {
                out_ << j.dump(2) << "\n";
                found = true;
            }
        }
        if (!found)
            throw std::invalid_argument("Job does not exist");
    } else if (c == "cancel") {
        count(2);
        engine_.cancel(number(arg(1)));
    } else if (c == "policy") {
        if (t.size() < 2 || t.size() > 3)
            throw std::invalid_argument("policy NAME [quantum]");
        engine_.policy(arg(1), t.size() == 3 ? number(t[2]) : 2);
    } else if (c == "mem-policy") {
        count(2);
        engine_.memoryPolicy(arg(1));
    } else if (c == "workers" || c == "slots") {
        count(2);
        engine_.configure({{c, number(arg(1))}});
    } else if (c == "mode") {
        count(2);
        engine_.configure({{"mode", arg(1)}});
    } else if (c == "delay") {
        count(2);
        engine_.configure({{"delay_ms", number(arg(1))}});
    } else if (c == "run" || c == "resume") {
        count(1);
        engine_.run();
    } else if (c == "pause") {
        count(1);
        engine_.pause();
    } else if (c == "step") {
        count(1);
        out_ << (engine_.step() ? "Advanced one tick.\n" : "No unfinished jobs.\n");
    } else if (c == "finish") {
        count(1);
        engine_.runUntilDone();
        report();
    } else if (c == "reset") {
        count(1);
        engine_.reset();
    } else if (c == "report") {
        count(1);
        report();
    } else if (c == "gantt") {
        count(1);
        gantt();
    } else if (c == "memmap") {
        count(1);
        out_ << engine_.snapshot()["memory"].dump(2) << "\n";
    } else if (c == "log") {
        count(1);
        auto snapshot = engine_.snapshot();
        for (const auto &e : snapshot["log"])
            out_ << "t=" << e["tick"] << " " << e["type"].get<std::string>() << " "
                 << e["message"].get<std::string>() << "\n";
    } else if (c == "load") {
        count(2);
        engine_.loadFile(arg(1));
    } else if (c == "demo") {
        count(2);
        engine_.loadDemo(arg(1));
        out_ << "Demo loaded; step or run to observe.\n";
    } else if (c == "compare") {
        count(1);
        out_ << engine_.compare().dump(2) << "\n";
    } else
        throw std::invalid_argument("Unknown command: " + c + ". Use help.");
    return true;
}
void CLI::interactive() {
    banner();
    help();
    std::string line;
    while (out_ << "\nbuildqueue › " && std::getline(std::cin, line)) {
        try {
            if (!command(line))
                break;
        } catch (const std::exception &e) {
            out_ << "Error: " << e.what() << "\n";
        }
    }
    engine_.pause();
}
} // namespace bq
