#include "Checksum.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace bq {
namespace {
class Descriptor {
    int fd_;

public:
    explicit Descriptor(int fd) : fd_(fd) {}
    ~Descriptor() { close(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int get() const { return fd_; }
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
};

class SpawnActions {
    posix_spawn_file_actions_t actions_;

    static void check(int error) {
        if (error)
            throw std::system_error(error, std::generic_category(), "Preparing checksum process");
    }

public:
    SpawnActions() { check(posix_spawn_file_actions_init(&actions_)); }
    ~SpawnActions() { posix_spawn_file_actions_destroy(&actions_); }
    void redirect(int from, int to) {
        check(posix_spawn_file_actions_adddup2(&actions_, from, to));
    }
    const posix_spawn_file_actions_t* get() const { return &actions_; }
};
}

Checksum::Checksum(const std::filesystem::path& folder)
    : root_(std::filesystem::canonical(folder)),
      directory_(::open(root_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {
    if (directory_ < 0)
        throw std::system_error(errno, std::generic_category(), "Opening input folder");
}

Checksum::~Checksum() { ::close(directory_); }

bool Checksum::validName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

std::vector<FileInfo> Checksum::files() const {
    std::vector<FileInfo> result;
    // Open the anchored directory through /proc so listing and processing use the same folder.
    const auto folder = "/proc/self/fd/" + std::to_string(directory_);
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(entry.symlink_status(error)))
            continue;
        auto size = entry.file_size(error);
        if (!error)
            result.push_back({entry.path().filename().string(), size});
    }
    std::sort(result.begin(), result.end(),
              [](const FileInfo& a, const FileInfo& b) { return a.name < b.name; });
    return result;
}

std::optional<std::uintmax_t> Checksum::size(const std::string& name) const {
    struct stat info {};
    if (!validName(name) || ::fstatat(directory_, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(info.st_mode) || info.st_size < 0)
        return std::nullopt;
    return static_cast<std::uintmax_t>(info.st_size);
}

std::optional<std::uintmax_t> Checksum::pathSize(const std::filesystem::path& path) {
    struct stat info {};
    if (!path.is_absolute() || path.native().find('\0') != std::string::npos ||
        ::lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0)
        return std::nullopt;
    return static_cast<std::uintmax_t>(info.st_size);
}

ChecksumResult Checksum::run(const std::string& name, std::chrono::milliseconds timeout) const {
    ChecksumResult result;
    if (!validName(name)) {
        result.error = "Choose a file directly inside the configured input folder.";
        return result;
    }

    return runInput(::openat(directory_, name.c_str(),
                             O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC), timeout);
}

ChecksumResult Checksum::runPath(const std::filesystem::path& path,
                                std::chrono::milliseconds timeout) {
    if (!path.is_absolute() || path.native().find('\0') != std::string::npos) {
        ChecksumResult result;
        result.error = "Choose a local file with an absolute path.";
        return result;
    }
    return runInput(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC), timeout);
}

ChecksumResult Checksum::runInput(int descriptor, std::chrono::milliseconds timeout) {
    ChecksumResult result;
    Descriptor input(descriptor);
    if (input.get() < 0) {
        result.error = std::string("Could not open file: ") + std::strerror(errno);
        return result;
    }
    struct stat info {};
    if (::fstat(input.get(), &info) != 0 || !S_ISREG(info.st_mode)) {
        result.error = "Input must be a regular file.";
        return result;
    }

    int pipeEnds[2];
    if (::pipe2(pipeEnds, O_CLOEXEC) != 0) {
        result.error = std::string("Could not open output pipe: ") + std::strerror(errno);
        return result;
    }
    Descriptor reader(pipeEnds[0]);
    Descriptor writer(pipeEnds[1]);
    Descriptor nullOutput(::open("/dev/null", O_WRONLY | O_CLOEXEC));
    if (nullOutput.get() < 0) {
        result.error = std::string("Could not open /dev/null: ") + std::strerror(errno);
        return result;
    }

    SpawnActions actions;
    actions.redirect(input.get(), STDIN_FILENO);
    actions.redirect(writer.get(), STDOUT_FILENO);
    actions.redirect(nullOutput.get(), STDERR_FILENO);
    char executable[] = "/usr/bin/cksum";
    char locale[] = "LC_ALL=C";
    char* argv[] = {executable, nullptr};
    char* env[] = {locale, nullptr};
    pid_t pid;
    int error = ::posix_spawn(&pid, executable, actions.get(), nullptr, argv, env);
    if (error) {
        result.error = std::string("Could not start cksum: ") + std::strerror(error);
        return result;
    }

    writer.close();
    result.pid = pid;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (true) {
        auto waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid)
            break;
        if (waited < 0 && errno != EINTR) {
            result.error = std::string("Could not wait for cksum: ") + std::strerror(errno);
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            result.exitCode = 124;
            result.error = "Checksum process timed out.";
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (result.exitCode != 0) {
        result.error = "cksum failed with exit " + std::to_string(result.exitCode) + ".";
        return result;
    }

    // With standard input and no filename arguments, cksum writes just two integers.
    std::string output;
    char buffer[128];
    for (;;) {
        auto count = ::read(reader.get(), buffer, sizeof(buffer));
        if (count > 0)
            output.append(buffer, static_cast<std::size_t>(count));
        else if (count == 0)
            break;
        else if (errno != EINTR) {
            result.error = "Could not read checksum output.";
            return result;
        }
    }
    std::istringstream stream(output);
    std::uint64_t checksum;
    std::string extra;
    if (!(stream >> checksum >> result.bytes) || checksum > std::numeric_limits<std::uint32_t>::max()
        || stream >> extra) {
        result.error = "Unexpected cksum output.";
        return result;
    }
    result.checksum = static_cast<std::uint32_t>(checksum);
    return result;
}
}
