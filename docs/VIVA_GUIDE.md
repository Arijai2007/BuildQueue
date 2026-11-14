# BuildQueue viva guide

Jaypee Institute of Information Technology · Operating Systems Lab · **24B15CS312** · PBL  
Arihant Jain — 992401030014; Yuvraj Rana — 992401030021; Siddharth Singh — 992401030026; Batch F1  
Submitted to Dr. Ambalika Sarkar

1. **What is BuildQueue's objective?** To integrate scheduling, memory admission, synchronization, real workers, optional processes and measurement in one explainable local job pipeline.
2. **How is it different from the reference?** It is original C++17 with a deterministic coordinator and persistent worker pool, contiguous allocation and an offline REST dashboard. The reference's signal/ucontext description was educational background; its source was not copied or independently audited.
3. **Why C++17?** It provides RAII, typed enums, STL, threads, mutexes, condition variables and futures while remaining widely supported. The counting semaphore is implemented because std::counting_semaphore needs C++20.
4. **What is a process?** An executing program with its own virtual address space and OS-managed resources. BuildQueue's live checksum creates a real POSIX process.
5. **What is a thread?** A schedulable execution stream within a process, sharing its address space with peer threads while maintaining its own execution context and stack.
6. **Process versus thread?** Processes normally isolate memory; threads share process memory and need synchronization for shared mutable data. Creating a simulated job record is not itself creating a Linux process.
7. **What is CPU scheduling here?** Choosing which READY jobs receive logical worker time according to FCFS, SJF, Priority or RR, subject to resource admission. Linux separately schedules the application's actual threads.
8. **Why is FCFS non-preemptive?** Once admitted, a job keeps its lane until its declared burst completes. Arrival then ID determines candidate order.
9. **Why is SJF non-preemptive here?** It chooses the shortest original burst only when a lane becomes free. A later short job cannot interrupt an active job; that would require a preemptive SRTF policy.
10. **How are priority ties resolved?** Lower numeric priority wins; arrival tick and then increasing ID break ties. The policy is non-preemptive.
11. **Why is Round Robin preemptive?** At the quantum boundary, unfinished simulated work releases its lane and slot and rejoins the READY FIFO queue, regardless of remaining burst.
12. **What does a small or large quantum change?** Small quanta generally improve responsiveness but cause more redispatches. A quantum larger than every burst approaches FCFS behavior for an unconstrained FIFO workload.
13. **What happens to arrivals at an RR boundary?** New arrivals join READY before the expired running job is requeued. This rule is deterministic and covered by assertions.
14. **Does BuildQueue perform real CPU context switches?** No. Its dispatch/preemption events model assignments; the Linux kernel performs real context switches. The report counts dispatches as context-switch-like transitions.
15. **How is starvation handled?** Resource-blocked jobs do not obstruct other runnable jobs. Finite fixtures drain. Priority aging is not implemented, so a continuous stream of higher-priority jobs may starve lower priorities.
16. **What is First Fit?** The allocator uses the first address-ordered free block large enough for the requested contiguous reservation.
17. **Best Fit versus Worst Fit?** Best Fit uses the smallest suitable hole; Worst Fit uses the largest. Equal-sized candidates use address order. Their fragmentation trade-offs depend on the workload.
18. **What is external fragmentation?** Free memory scattered into separate holes. A large contiguous request can fail despite sufficient total free capacity. The demo has 580 MB free and a largest hole of 300 MB, blocking a 400 MB request.
19. **Why is internal fragmentation zero?** Variable partitions are split to exactly the requested size. There is no rounding or fixed allocation unit inside an allocated block.
20. **What is coalescing?** Merging adjacent free blocks after deallocation. It restores larger contiguous holes without moving allocated jobs.
21. **Does BuildQueue allocate physical RAM in MB?** No. These are simulated reservations and offsets. Real C++ containers consume host memory separately, under the OS's normal allocator and virtual-memory management.
22. **Mutex versus semaphore?** A mutex protects exclusive access to a critical section. A counting semaphore represents a number of permits, allowing at most N admitted users of a resource.
23. **How is the semaphore implemented?** A mutex protects the permit count, a predicate condition-variable wait sleeps until a permit exists, and release increments the count and notifies one waiter. Over-release is rejected.
24. **What is a race condition?** A correctness failure caused by unsynchronized access whose result depends on timing. Shared queue state is mutex-protected; the worker counter uses atomic increments.
25. **How does the counter demonstrate synchronization?** Each assigned worker tick increments it exactly once. The contention fixture ends at 18 increments and 18 busy-worker ticks; the separate mutex/semaphore stress test completes 1600 increments without loss.
26. **How are deadlocks prevented?** The coordinator uses nonblocking admission, releases a provisional slot on memory failure, takes manager locks one at a time, and workers never acquire the engine mutex. No circular lock dependency exists.
27. **Why do jobs enter WAITING?** A READY job is denied either a contiguous memory block or an execution permit. Its explicit reason is MEMORY_UNAVAILABLE or RESOURCE_UNAVAILABLE, and it consumes no CPU tick while blocked.
28. **Why acquire memory before execution?** The declared workload is eligible to run only after its required resources are reserved. Scheduling selects a candidate; admission determines whether it can actually execute.
29. **How do waiting jobs resume?** Resource release notifies the coordinator; active execution retries WAITING jobs at deterministic tick boundaries. Paused simulations advance only after resume or step.
30. **Why retain memory across RR quanta?** It models a process keeping its address-space reservation while waiting for CPU again. Execution slots are released each quantum, allowing another admitted job to run.
31. **How is CPU utilization calculated?** Busy logical worker ticks divided by available logical worker ticks, multiplied by 100. For fixed workers, denominator is worker count × elapsed ticks. Historical capacity is accumulated if workers are safely resized.
32. **What are turnaround, response and waiting?** Turnaround is completion−arrival; response is first execution−arrival; READY wait and resource wait count time in their respective states. Completed simulated jobs satisfy turnaround = burst + both waits.
33. **Why separate READY and resource waiting?** Policy delay and resource shortage have different causes. Combining them would hide memory/semaphore bottlenecks.
34. **Logical time versus wall time?** Logical ticks decide deterministic scheduling. Wall milliseconds describe actual runtime and are recorded separately; playback delay changes presentation speed without changing logical results.
35. **How do fork and exec differ?** fork creates a child process. exec replaces that child's program image with the requested executable. The parent uses waitpid to collect exit status and prevent a zombie.
36. **Why avoid arbitrary shell commands?** Fixed validated argv makes the process demonstration controlled and explainable. The child runs cksum on a fixed fixture; no system(), sh -c, or untrusted command construction is used.
37. **What is safe between fork and exec in a threaded process?** The child uses only async-signal-safe descriptor operations, execve and _exit. Arguments and descriptors are prepared before fork; no C++ locks, heap allocation or streams are used by the child.
38. **How are process timeouts handled?** The parent checks waitpid with WNOHANG, sleeps briefly between checks, and after three seconds sends SIGKILL and reaps the child. Nonzero exit and timeout errors remain visible.
39. **Can a running job be cancelled?** No. NEW, READY and WAITING may be cancelled. A RUNNING job must finish or reach an RR boundary. Reset is an explicit experiment teardown after a safe tick boundary.
40. **What does reset do?** It pauses, clears jobs, metrics and Gantt history, returns slots and memory, and preserves configuration. IDs remain unique and the persistent log is retained.
41. **How is comparison fair?** It snapshots original job inputs and configuration, creates four fresh engines, starts each at tick zero and runs identical worker/memory/resource settings. Parent state is unchanged.
42. **Why persistent workers?** Creating a thread every quantum confuses scheduling with thread startup overhead. A protected queue and condition variable reuse existing threads for every assigned tick.
43. **What is the condition-variable predicate for the pool?** Stop requested or queue nonempty. It prevents lost wakeups and handles spurious wakeups correctly.
44. **What are the main limitations?** Simulated RAM and CPU ticks, known burst estimates, no kernel scheduler control, no real-process preemption, no priority aging, in-memory experiment state and bounded educational workloads. Sanitizer runtime support is environment-dependent.
45. **Which result can be calculated by hand?** FCFS arrivals 0/1/2 and bursts 4/3/2 yield waits 0/3/5, completions 4/7/9, average wait 8/3, average turnaround 17/3 and utilization 100% on one worker.

46. **Why does a log-write failure not interrupt admission?** Throwing after a semaphore permit is acquired can leave half-finished state. BuildQueue records an explicit logging warning, keeps events in memory and completes state/resource bookkeeping; persistent logging resumes after storage is restored and the engine restarts.
47. **Why does one dashboard use an exclusive port?** Shared-port listening could send successive requests to different engine instances. BuildQueue omits SO_REUSEPORT so a conflicting instance fails with a clear diagnostic.
48. **How does the dashboard avoid stale comparison results?** The backend supplies an experiment revision and captures it in comparison output. The frontend discards earlier refreshes around mutations and invalidates comparisons when the revision changes.
