#pragma once
#include "Job.hpp"
#include <deque>
namespace bq {
class Logger {
    int fd_ = -1;
    bool enabled_ = false;
    std::string error_;
    std::deque<json> events_;
    std::uint64_t sequence_ = 0;

  public:
    explicit Logger(const std::string &file);
    ~Logger();
    void record(int tick, const std::string &type, int job, const std::string &message);
    json snapshot() const;
    json status() const;
    void clear() {
        events_.clear();
    }
};
} // namespace bq
