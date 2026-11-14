#include "Scenario.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace bq {
std::vector<JobSpec> readCsv(const std::string &path) {
    std::ifstream f(path);
    if (!f)
        throw std::invalid_argument("Cannot open CSV: " + path);
    std::vector<JobSpec> batch;
    std::string line;
    int row = 0;
    while (std::getline(f, line)) {
        ++row;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        if (line == "name,arrival,burst,priority,memory,type")
            continue;
        std::vector<std::string> cells;
        std::istringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ','))
            cells.push_back(cell);
        if (cells.size() != 6)
            throw std::invalid_argument("CSV row " + std::to_string(row) + " requires six fields");
        auto number = [&](int i) {
            std::size_t consumed = 0;
            int n = std::stoi(cells.at(static_cast<std::size_t>(i)), &consumed);
            if (consumed != cells.at(static_cast<std::size_t>(i)).size())
                throw std::invalid_argument("Invalid CSV integer");
            return n;
        };
        JobSpec s;
        s.name = cells[0];
        s.arrival = number(1);
        s.burst = number(2);
        s.priority = number(3);
        s.memory = number(4);
        s.type = cells[5];
        batch.push_back(s);
        if (batch.size() > 500)
            throw std::invalid_argument("Maximum 500 CSV rows");
    }
    if (batch.empty())
        throw std::invalid_argument("CSV must have at least one job");
    return batch;
}
} // namespace bq
