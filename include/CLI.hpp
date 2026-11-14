#pragma once
#include "BuildQueueEngine.hpp"
#include <ostream>
namespace bq {
class CLI {
    BuildQueueEngine &engine_;
    std::ostream &out_;

  public:
    explicit CLI(BuildQueueEngine &e, std::ostream &out) : engine_(e), out_(out) {}
    void banner();
    void help();
    void report();
    void gantt();
    bool command(const std::string &);
    void interactive();
};
} // namespace bq
