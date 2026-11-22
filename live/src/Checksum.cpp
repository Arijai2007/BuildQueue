#include "Checksum.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/socket.h>
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

ChecksumResult Checksum::run(const std::string& name, std::chrono::milliseconds timeout,
                             std::uint64_t addressSpaceBytes) const {
    ChecksumResult result;
    if (!validName(name)) {
        result.error = "Choose a file directly inside the configured input folder.";
        return result;
    }

    return runInput(::openat(directory_, name.c_str(),
                             O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC), timeout, addressSpaceBytes, pool_);
}

ChecksumResult Checksum::runPath(const std::filesystem::path& path,
                                std::chrono::milliseconds timeout, std::uint64_t addressSpaceBytes,
                                BufferPool* pool) {
    if (!path.is_absolute() || path.native().find('\0') != std::string::npos) {
        ChecksumResult result;
        result.error = "Choose a local file with an absolute path.";
        return result;
    }
    static BufferPool defaultPool;
    return runInput(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC),
                    timeout, addressSpaceBytes, pool ? *pool : defaultPool);
}

ChecksumResult Checksum::runInput(int descriptor, std::chrono::milliseconds timeout,
                                  std::uint64_t addressSpaceBytes, BufferPool& pool) try {
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
    const auto chunk = std::clamp<std::uintmax_t>(info.st_size, 4096, BufferPool::maxBuffer);
    const auto deadline = BufferPool::Clock::now() + timeout;
    auto buffer = pool.acquire(chunk, deadline);
    if (BufferPool::Clock::now() >= deadline)
        throw BufferPool::WaitTimeout();
    int streamEnds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, streamEnds) != 0) {
        result.error = "Could not create the checksum input stream.";
        return result;
    }
    Descriptor childInput(streamEnds[0]);
    Descriptor feed(streamEnds[1]);

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
    actions.redirect(childInput.get(), STDIN_FILENO);
    actions.redirect(writer.get(), STDOUT_FILENO);
    actions.redirect(nullOutput.get(), STDERR_FILENO);
    char executable[] = "/usr/bin/prlimit";
    auto limit = "--as=" + std::to_string(addressSpaceBytes) + ':' + std::to_string(addressSpaceBytes);
    char separator[] = "--";
    char checksumProgram[] = "/usr/bin/cksum";
    char locale[] = "LC_ALL=C";
    char* argv[] = {executable, limit.data(), separator, checksumProgram, nullptr};
    char* env[] = {locale, nullptr};
    pid_t pid;
    int error = ::posix_spawn(&pid, executable, actions.get(), nullptr, argv, env);
    if (error) {
        result.error = std::string("Could not start prlimit: ") + std::strerror(error);
        return result;
    }

    writer.close();
    childInput.close();
    result.pid = pid;
    result.addressSpaceLimitBytes = addressSpaceBytes;
    int status = 0;
    struct rusage usage {};
    std::size_t sent = 0, pending = 0;
    while (true) {
        auto waited = ::wait4(pid, &status, WNOHANG, &usage);
        if (waited == pid) {
            result.peakRssKiB = usage.ru_maxrss;
            break;
        }
        if (waited < 0 && errno != EINTR) {
            result.error = std::string("Could not wait for cksum: ") + std::strerror(errno);
            return result;
        }
        const bool timedOut = std::chrono::steady_clock::now() >= deadline;
        if (timedOut || !result.error.empty()) {
            ::kill(pid, SIGKILL);
            do {
                waited = ::wait4(pid, &status, 0, &usage);
            } while (waited < 0 && errno == EINTR);
            if (waited == pid)
                result.peakRssKiB = usage.ru_maxrss;
            result.exitCode = timedOut ? 124 : 1;
            if (timedOut)
                result.error = "Checksum process timed out.";
            return result;
        }
        if (feed.get() >= 0 && sent == pending) {
            const auto count = ::read(input.get(), buffer.data(), buffer.size());
            if (count == 0)
                feed.close();
            else if (count > 0) {
                sent = 0;
                pending = static_cast<std::size_t>(count);
            } else if (errno != EINTR) {
                result.error = "Could not read checksum input.";
                continue;
            }
        }
        if (feed.get() >= 0 && sent < pending) {
            const auto count = ::send(feed.get(), buffer.data() + sent, pending - sent,
                                      MSG_DONTWAIT | MSG_NOSIGNAL);
            if (count > 0) {
                sent += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EPIPE)
                feed.close();
            else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                result.error = "Could not send input to the checksum process.";
                continue;
            }
        }
        if (feed.get() >= 0 && sent < pending) {
            pollfd inputWritable{feed.get(), POLLOUT, 0};
            if (::poll(&inputWritable, 1, 5) < 0 && errno != EINTR)
                result.error = "Could not wait for checksum input to become writable.";
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (result.exitCode != 0) {
        result.error = "Checksum job failed with exit " + std::to_string(result.exitCode) +
                       " under a " + std::to_string(addressSpaceBytes) + "-byte address-space limit.";
        return result;
    }

    // With standard input and no filename arguments, cksum writes just two integers.
    std::string output;
    char outputChunk[128];
    for (;;) {
        auto count = ::read(reader.get(), outputChunk, sizeof(outputChunk));
        if (count > 0)
            output.append(outputChunk, static_cast<std::size_t>(count));
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
} catch (const BufferPool::WaitTimeout& error) {
    ChecksumResult result;
    result.exitCode = 124;
    result.error = error.what();
    return result;
}
}
