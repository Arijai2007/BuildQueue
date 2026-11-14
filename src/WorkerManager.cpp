#include "WorkerManager.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
namespace bq {
WorkerManager::WorkerManager(int n) {
    resize(n);
}
WorkerManager::~WorkerManager() {
    shutdown();
}
void WorkerManager::shutdown() {
    {
        std::lock_guard<std::mutex> l(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto &t : threads_)
        if (t.joinable())
            t.join();
    threads_.clear();
}
void WorkerManager::resize(int n) {
    if (n < 1 || n > 16)
        throw std::invalid_argument("Workers must be 1–16");
    shutdown();
    {
        std::lock_guard<std::mutex> l(mutex_);
        stopping_ = false;
    }
    try {
        for (int i = 0; i < n; ++i)
            threads_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> l(mutex_);
                        cv_.wait(l, [this] { return stopping_ || !queue_.empty(); });
                        if (stopping_ && queue_.empty())
                            return;
                        task = std::move(queue_.front());
                        queue_.pop();
                    }
                    task();
                }
            });
    } catch (...) {
        shutdown();
        throw;
    }
}
std::future<WorkResult> WorkerManager::submit(std::function<WorkResult()> fn) {
    auto task = std::make_shared<std::packaged_task<WorkResult()>>(std::move(fn));
    auto f = task->get_future();
    {
        std::lock_guard<std::mutex> l(mutex_);
        if (stopping_)
            throw std::runtime_error("Worker pool stopped");
        queue_.emplace([task] { (*task)(); });
    }
    cv_.notify_one();
    return f;
}
WorkResult WorkerManager::execute(bool process, bool live, const std::string &path) {
    WorkResult r;
    auto start = std::chrono::steady_clock::now();
    if (process) {
        // Build argv and open descriptors BEFORE fork. Child does only async-signal-safe calls.
        char program[] = "/usr/bin/cksum";
        char *argv[] = {program, const_cast<char *>(path.c_str()), nullptr};
        char locale[] = "LC_ALL=C";
        char *childEnvironment[] = {locale, nullptr};
        int sink = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (sink < 0) {
            r.exitCode = 127;
            r.error = std::string("Cannot open process output sink: ") + std::strerror(errno);
            r.wallMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            return r;
        }
        pid_t pid = ::fork();
        if (pid == 0) {
            if (::dup2(sink, STDOUT_FILENO) < 0 || ::dup2(sink, STDERR_FILENO) < 0)
                ::_exit(126);
            ::close(sink);
            ::execve(program, argv, childEnvironment);
            ::_exit(127);
        }
        int forkError = errno;
        ::close(sink);
        r.pid = static_cast<int>(pid);
        if (pid < 0) {
            r.exitCode = 127;
            r.error = std::string("fork failed: ") + std::strerror(forkError);
        } else {
            int status = 0;
            bool finished = false;
            while (!finished) {
                auto w = ::waitpid(pid, &status, WNOHANG);
                if (w == pid) {
                    finished = true;
                    break;
                }
                if (w < 0) {
                    if (errno == EINTR)
                        continue;
                    r.error = "waitpid failed";
                    r.exitCode = 127;
                    break;
                }
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3)) {
                    ::kill(pid, SIGKILL);
                    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                    }
                    r.exitCode = 124;
                    r.error = "Controlled process exceeded 3-second timeout";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (finished) {
                r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                if (r.exitCode)
                    r.error = "cksum failed with exit " + std::to_string(r.exitCode);
            }
        }
    } else {
        volatile std::uint64_t checksum = 1;
        for (int k = 0; k < (live ? 200000 : 1000); ++k)
            checksum = checksum * 1664525U + 1013904223U;
        (void)checksum;
    }
    r.wallMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return r;
}
} // namespace bq
