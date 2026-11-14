#pragma once
#include "Job.hpp"
namespace bq {
std::vector<JobSpec> readCsv(const std::string &path);
}
