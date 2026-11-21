#include "BatchQueue.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace bq {
BatchQueue::BatchQueue(int workers, int slots, Work work, Input input, Estimate estimate)
    : slots_(slots), input_(input), work_(std::move(work)), estimate_(std::move(estimate)),
      permits_(slots, detector_),
      mutex_(detector_, "job queue") {
    if (workers < 1 || workers > 16 || slots > workers || !work_)
        throw std::invalid_argument("Use 1 to 16 workers and 1 to workers slots.");
    try {
        for (int i = 0; i < workers; ++i)
            workers_.emplace_back(&BatchQueue::worker, this);
    } catch (...) {
        stop();
        throw;
    }
}

BatchQueue::~BatchQueue() { stop(); }

void BatchQueue::stop() {
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        stopping_ = true;
    }
    changed_.notify_all();
    for (auto& worker : workers_)
        worker.join();
}

void BatchQueue::setPolicy(Policy policy) {
    std::lock_guard<TrackedMutex> lock(mutex_);
    policy_ = policy;
}

std::vector<std::uint64_t> BatchQueue::submit(const std::vector<std::string>& files, int priority) {
    if (files.empty() || files.size() > 100)
        throw std::invalid_argument("Submit 1 to 100 files per batch.");
    std::vector<JobRequest> requests;
    requests.reserve(files.size());
    for (const auto& file : files)
        requests.push_back({file, priority});
    return submit(requests);
}

std::vector<std::uint64_t> BatchQueue::submit(const std::vector<JobRequest>& requests) {
    if (requests.empty() || requests.size() > 100)
        throw std::invalid_argument("Submit 1 to 100 files per batch.");
    std::vector<Job> batch;
    batch.reserve(requests.size());
    for (const auto& [file, priority] : requests) {
        if (priority < 0 || priority > 9)
            throw std::invalid_argument("Use priorities from 0 to 9.");
        if (input_ == Input::FolderNames && !Checksum::validName(file))
            throw std::invalid_argument("Use file names from the input folder.");
        if (input_ == Input::AbsolutePaths &&
            (!std::filesystem::path(file).is_absolute() || file.find('\0') != std::string::npos))
            throw std::invalid_argument("Choose local files with absolute paths.");
        Job job;
        job.file = file;
        job.priority = priority;
        if (estimate_)
            job.estimatedBytes = estimate_(file);
        batch.push_back(std::move(job));
    }

    std::vector<std::uint64_t> ids;
    ids.reserve(batch.size());
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        if (jobs_.size() + batch.size() > 500)
            throw std::invalid_argument("Queue history is full. Clear finished jobs and retry.");
        jobs_.reserve(jobs_.size() + batch.size());
        const auto now = Job::Clock::now();
        for (auto& job : batch) {
            job.id = nextId_++;
            job.submitted = now;
            ids.push_back(job.id);
            jobs_.push_back(std::move(job));
        }
        queued_ += static_cast<int>(batch.size());
    }
    changed_.notify_all();
    return ids;
}

bool BatchQueue::terminal(State state) {
    return state == State::Completed || state == State::Failed || state == State::Cancelled;
}

bool BatchQueue::cancel(std::uint64_t id) {
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto job = std::find_if(jobs_.begin(), jobs_.end(), [id](const Job& j) { return j.id == id; });
        if (job == jobs_.end() || job->state != State::Queued)
            return false;
        job->state = State::Cancelled;
        job->finished = Job::Clock::now();
        --queued_;
    }
    changed_.notify_all();
    return true;
}

void BatchQueue::clearFinished() {
    std::lock_guard<TrackedMutex> lock(mutex_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                              [](const Job& job) { return terminal(job.state); }),
                jobs_.end());
}

Snapshot BatchQueue::snapshot() const {
    std::lock_guard<TrackedMutex> lock(mutex_);
    return {jobs_, static_cast<int>(workers_.size()), slots_, running_, peak_, policy_,
            detector_.report()};
}

void BatchQueue::waitUntilIdle() {
    std::unique_lock<TrackedMutex> lock(mutex_);
    changed_.wait(lock, [this] { return queued_ == 0 && running_ == 0; });
}

std::vector<Job>::iterator BatchQueue::nextJob() {
    auto selected = jobs_.end();
    for (auto job = jobs_.begin(); job != jobs_.end(); ++job) {
        if (job->state != State::Queued)
            continue;
        if (selected == jobs_.end()) {
            selected = job;
            continue;
        }
        const bool higherPriority = policy_ == Policy::Priority && job->priority < selected->priority;
        const bool shorter = policy_ == Policy::Sjf && job->estimatedBytes &&
            (!selected->estimatedBytes || *job->estimatedBytes < *selected->estimatedBytes);
        if (higherPriority || shorter)
            selected = job;
    }
    return selected;
}

void BatchQueue::worker() {
    for (;;) {
        std::uint64_t id;
        std::string file;
        {
            std::unique_lock<TrackedMutex> lock(mutex_);
            changed_.wait(lock, [this] {
                return stopping_ || (queued_ > 0 && permits_.tryAcquire());
            });
            if (stopping_)
                return;
            auto job = nextJob();
            id = job->id;
            file = job->file;
            job->state = State::Running;
            job->dispatchOrder = nextDispatch_++;
            job->dispatchPolicy = policy_;
            job->started = Job::Clock::now();
            --queued_;
            ++running_;
            peak_ = std::max(peak_, running_);
        }

        ChecksumResult result;
        try {
            result = work_(file);
        } catch (const std::exception& error) {
            result.error = error.what();
        }

        {
            std::lock_guard<TrackedMutex> lock(mutex_);
            auto job = std::find_if(jobs_.begin(), jobs_.end(), [id](const Job& j) { return j.id == id; });
            job->state = result.error.empty() && result.exitCode == 0 ? State::Completed : State::Failed;
            job->result = std::move(result);
            job->finished = Job::Clock::now();
            --running_;
            permits_.release();
        }
        changed_.notify_all();
    }
}
}
