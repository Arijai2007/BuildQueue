# Originality and reference comparison

BuildQueue — An Intelligent Local Job & Resource Management System is an original C++17 PBL implementation for Jaypee Institute of Information Technology, Operating Systems Lab, 24B15CS312. Arihant Jain, Yuvraj Rana and Siddharth Singh (F1) submit it to Dr. Ambalika Sarkar.

The named educational reference is https://github.com/KevinFu12/Operating-System-TimeSharing. No local copy was present when the workspace was inspected. An attempted remote page read was unavailable, so the reference column below is based on the **description in the supplied brief**, not an independent source-code audit. Downloading that repository was not made a prerequisite. No reference implementation, structure, comments, scheduler.c, task.c or main.c were copied or modified.

| Area | Reference as described in the brief | BuildQueue implementation |
|---|---|---|
| Language | C | Original modular C++17, RAII and STL |
| Execution model | ucontext and timer signals | Integer-tick coordinator, persistent std::thread pool, future barrier |
| Scheduling | FCFS, SJF, RR, Priority, MLFQ | Four required Strategy classes; MLFQ left as future work |
| Resources | Simulated locks | Counted semaphore admission integrated with contiguous memory |
| Memory | No claim made without source inspection | First/Best/Worst Fit, split/coalesce, ownership, actual fragmentation failures |
| Timing | Signal-driven model as described | Deterministic logical scheduling plus explicitly nondeterministic live child timing |
| Interfaces | No independent UI claim | Shared CLI/REST engine and offline HTML/CSS/JS dashboard |
| Verification | No upstream testing claim | Self-contained lab tests, exact FCFS values, real HTTP/CLI checks, Linux builds |
| Processes | No source-audited claim | Controlled fixed-argv fork/execve/waitpid with timeout and reaping |

BuildQueue does not use signal-driven context switching to imitate a kernel. SIGINT/SIGTERM are used only for clean server termination. Dispatches are honestly labelled context-switch-like simulation events. All third-party code is limited to approved cpp-httplib and nlohmann/json headers, with their original MIT notices retained in third_party/LICENSES. BuildQueue's own MIT license does not claim ownership over these dependencies or the reference repository.
