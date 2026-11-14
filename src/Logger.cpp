#include "Logger.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
namespace bq {
Logger::Logger(const std::string &file) {
    enabled_ = !file.empty();
    if (!file.empty()) {
        fd_ = ::open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open log file: " + file);
    }
}
Logger::~Logger() {
    if (fd_ >= 0)
        ::close(fd_);
}
void Logger::record(int tick, const std::string &type, int job, const std::string &message) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    json e = {{"sequence", ++sequence_},
              {"tick", tick},
              {"wall_epoch_ms", ms},
              {"type", type},
              {"job", job},
              {"message", message}};
    if (fd_ >= 0) {
        std::string line = e.dump() + "\n";
        std::size_t pos = 0;
        while (pos < line.size()) {
            auto n = ::write(fd_, line.data() + pos, line.size() - pos);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                error_ = std::string("Execution log write failed: ") +
                         (n < 0 ? std::strerror(errno) : "zero-byte write");
                ::close(fd_);
                fd_ = -1;
                break;
            }
            pos += static_cast<std::size_t>(n);
        }
    }
    events_.push_back(std::move(e));
    if (events_.size() > 4000)
        events_.pop_front();
}
json Logger::status() const {
    return {{"enabled", enabled_},
            {"healthy", error_.empty()},
            {"persisting", fd_ >= 0},
            {"error", error_}};
}
json Logger::snapshot() const {
    json a = json::array();
    for (const auto &e : events_)
        a.push_back(e);
    return a;
}
} // namespace bq
