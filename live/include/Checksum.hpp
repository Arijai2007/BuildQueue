#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bq {
struct FileInfo {
    std::string name;
    std::uintmax_t size;
};

struct ChecksumResult {
    int pid = -1;
    int exitCode = -1;
    std::uint32_t checksum = 0;
    std::uintmax_t bytes = 0;
    std::string error;
};

class Checksum {
    std::filesystem::path root_;
    int directory_;
    static ChecksumResult runInput(int descriptor, std::chrono::milliseconds timeout);

public:
    explicit Checksum(const std::filesystem::path& folder);
    ~Checksum();
    Checksum(const Checksum&) = delete;
    Checksum& operator=(const Checksum&) = delete;

    std::vector<FileInfo> files() const;
    std::optional<std::uintmax_t> size(const std::string& name) const;
    static std::optional<std::uintmax_t> pathSize(const std::filesystem::path& path);
    static bool validName(const std::string& name);
    ChecksumResult run(const std::string& name,
                       std::chrono::milliseconds timeout = std::chrono::minutes(2)) const;
    static ChecksumResult runPath(
        const std::filesystem::path& path,
        std::chrono::milliseconds timeout = std::chrono::minutes(2));
};
}
