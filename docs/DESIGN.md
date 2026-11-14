# BuildQueue design

## Job lifecycle and validation

`JobState` is a typed enum. `Job::transition` explicitly accepts only:

```text
NEW     → READY | CANCELLED
READY   → RUNNING | WAITING | CANCELLED
WAITING → READY | CANCELLED
RUNNING → READY | COMPLETED
```

COMPLETED and CANCELLED are terminal. A normal job can complete without ever entering WAITING. A queued job can be cancelled; a RUNNING job cannot. RR jobs become cancellable between quanta. Cancellation releases any retained memory. Completion releases memory and its semaphore slot. Reset is a separate experiment teardown operation, allowed to reclaim all jobs after any in-progress tick transaction finishes.

IDs increase monotonically for an engine lifetime and do not reset. Initial jobs with reached arrival times become READY immediately; future jobs remain NEW. Names are 1–80 printable characters, burst 1–10000 ticks, priority 0–99, arrival 0–100000 and no earlier than the current tick, memory 1–configured capacity MB. Type is `simulated` or `process`; process jobs require live mode. The experiment holds at most 500 jobs. JSON integers are type-checked: fractional, boolean and string substitutes are rejected. CSV imports parse and validate the entire batch before mutating jobs. Demo files are also read and validated before resetting the current experiment.

## Scheduling strategies

| Policy | Preemptive? | Deterministic sort key |
|---|---|---|
| FCFS | No | arrival tick, ID |
| SJF | No | original burst, arrival tick, ID |
| Priority | No | numeric priority ascending, arrival tick, ID |
| RR | Yes, simulation only | READY insertion sequence, ID |

SJF is not shortest remaining time first. Priority does not interrupt active work and does not implement aging. For a finite submitted workload it drains; continued high-priority arrivals may starve lower priorities. Future arrivals cannot be scheduled early. If no eligible job can run, every unused logical lane receives an explicit idle event.

RR uses a default quantum of two ticks and persistent threads. At a boundary, new arrivals enter READY before an unfinished running job rejoins the queue. Memory reservations survive quanta until completion/cancellation; build slots do not. WAITING jobs retry at logical boundaries only when a slot and a suitable memory block (or retained reservation) are potentially available, and enter READY with a fresh FIFO sequence. Failed jobs do not consume CPU work and cannot block consideration of the rest of the READY list. Completion at a quantum boundary takes precedence over preemption.

Changing policy preserves job states, lane owners and remaining work. Existing non-preemptive owners remain on their lanes. Quantum counters restart; switching to RR allows unfinished running work at most the newly configured quantum before its next preemption. READY ordering is recalculated with the new strategy. Switching to a non-preemptive strategy lets current owners finish.

## Memory data structure

`MemoryManager` stores an address-ordered `vector<MemoryBlock>` with `{offset,size,owner}`. Owner zero denotes free memory. First Fit scans in address order; Best/Worst Fit scan for the smallest/largest suitable free block, with address-order ties. Allocation splits the chosen block to exactly the requested size and places any remainder immediately after it. Release verifies ownership, marks the block free, and repeatedly merges adjacent holes.

At all times, block offsets form a contiguous, non-overlapping partition of `[0,capacity)`, sizes are positive, and allocated plus free size equals capacity. Duplicate-owner allocation is rejected, nonexistent-owner release returns false, and double free cannot enlarge memory. The manager has its own mutex for independent concurrent use as well as coordinated engine access.

Internal fragmentation is **zero MB** for these exact variable partitions. External fragmentation is the indicator `100 × (1 − largest_free / total_free)` when free memory is positive, otherwise zero. It measures how scattered the free capacity is; it is not a count of inaccessible physical RAM. A request fails if no single free block fits even when the sum of holes is sufficient. The fragmentation CSV makes that observable without synthetic deallocation actions.

## Synchronization and lock order

1. Engine transaction mutex may acquire one manager mutex at a time: semaphore, memory or pool queue. Manager operations return before another manager lock is taken.
2. Worker threads acquire only their queue mutex, pop work, release it, execute work and update an atomic counter. They never take the engine mutex.
3. Semaphore waits use its own predicate CV; queue waits use only the queue mutex. Both release their mutex while sleeping.
4. Coordinator delay/pause waits use its own condition variable and release the engine mutex. APIs notify it for run/pause, reset and resource updates.

Logical admission uses `tryAcquire`, not a blocking semaphore wait while holding engine/memory locks. It obtains a provisional execution permit, allocates memory if necessary, and releases the permit immediately if memory fails. Completion releases the execution permit and memory. No worker sleeps waiting for a manager while holding another manager lock. The engine does wait for worker futures with the transaction mutex held, but workers have no path back to that mutex: the dependency is one-way, prevents partial snapshots, and has no circular acquisition. Live tasks have a bounded process timeout.

Resource waiters are coordinator-owned states rather than blocked per-job OS threads. Retries happen at tick/resource-release boundaries. An entirely paused simulation waits on a CV; active simulation does no CPU spin waiting for free resources. Thread wakeup order never chooses a logical job. The standalone synchronization test uses actual semaphore blocking, and concurrent memory stress exercises the allocator's independent mutex.

## Simulation versus live execution

Simulation is a synchronous integer-tick state machine. Persistent workers perform small deterministic CPU work, and the coordinator waits for the whole batch. Their wakeup order cannot influence arrival, ordering, preemption or Gantt events. Visualization delay is a CV timeout that affects playback speed only. Replaying a workload in a fresh engine produces identical jobs, metrics and Gantt events, excluding wall timestamps. Replaying on an existing engine preserves increasing IDs; normalize IDs by submission position to compare traces.

Live mode reuses job admission, queue strategies and accounting, but performs real C++ CPU work and optional real POSIX children. Several tasks/processes in a batch can run concurrently. Their wall times and PIDs are nondeterministic. Non-preemptive policies only are allowed. Declared burst ticks are logical accounting estimates, including for process jobs; actual process duration is separately reported in milliseconds. Use a one-tick burst for the simple process demonstration. Logical CPU utilization never claims to measure host CPU usage.

The predefined command is `/usr/bin/cksum <fixed-root>/demos/process_input.txt`, with argv assembled before `fork`. The parent opens `/dev/null` with close-on-exec; the child performs only `dup2`, `close`, `execve` and `_exit`. No untrusted shell, command text or filesystem path is accepted over HTTP. `waitpid(WNOHANG)` is checked at two-millisecond intervals until exit or the three-second deadline. Timeout sends SIGKILL and reaps the child with EINTR handling. Exit, PID, duration and errors are exposed. A process failure is logged as PROCESS_ERROR; the job still reaches COMPLETED as an execution record, with `process_exit` distinguishing success from failure.

## Metrics and exact interval accounting

| Metric | Definition |
|---|---|
| Turnaround | completion − arrival |
| Response | first run − arrival |
| READY waiting | Count of intervals in READY after dispatch decisions |
| Resource waiting | Count of intervals in WAITING after denied admission |
| CPU utilization | 100 × busy lane ticks / available lane ticks |
| Available lane ticks | Sum of configured logical worker count over elapsed intervals; equals workers × ticks for fixed configuration |
| Memory utilization | 100 × allocated memory / capacity |
| Throughput | completed jobs / elapsed logical ticks |
| External fragmentation | 100 × (1 − largest hole / total free), zero if no free memory |
| Context-switch-like transitions | Dispatch count, including first dispatch and redispatch; not a kernel context-switch count |
| Contention events | Denied admission attempts, including repeated retries; not unique blocked jobs |

For each completed simulated job: `turnaround = burst + READY wait + resource wait`. First run and completion are null-derived/sentinel values for incomplete work; averages include completed jobs only. When there are no completed samples, displayed averages are zero with sample count zero. A cancelled job retains observed waiting and first-run history, but has no fabricated turnaround or completion tick. Memory utilization is instantaneous; peak memory is tracked across allocations.

Worker-count changes use accumulated lane-capacity ticks, so historical utilization remains correct across a safe resize. Resizing workers/slots requires a paused engine and no occupied lane. Capacity or mode changes require no jobs. Reset preserves policies and configuration, clears metrics/Gantt/in-memory events, and reclaims reservations and slots.

## Logging, bounds and error handling

Every significant event has logical tick, monotonic event sequence and wall epoch milliseconds. POSIX `open(O_APPEND|O_CLOEXEC)` and partial-write/EINTR-safe `write` persist JSON Lines to `logs/buildqueue.log`. The engine mutex orders writes. Log-open failures abort startup. A disk write failure disables further persistent writes, retains the event stream in memory and exposes `logging.error` in snapshots and the dashboard. It does not throw halfway through resource admission; this prevents leaked provisional permits. Restoring disk logging requires a restart. Reset and Clear View do not truncate disk history. Only the latest 4000 events stay in memory; Gantt history remains until reset.

Server bind and missing-asset failures give explicit diagnostics. HTTP accepts at most 64 KiB per body, validates JSON through the same interface used by C++ tests, rejects cross-origin mutations and non-JSON POST content, and binds to 127.0.0.1. The application stops at 500000 ticks to bound an experiment; long histories can still be large, so the supplied workloads are deliberately compact. No external database or restoration protocol is included.

## Final-audit interface refinements

`arrival_now:true` is an opt-in API flag assigned under the engine transaction lock. Dashboard default arrivals and CLI submissions without an explicit arrival use it, avoiding a stale-tick race during playback. Original API requests retain their arrival-zero default.

`experiment_revision` increments on changes to original inputs or experiment policy/resource settings and on reset. Tick playback and visualization delay alone preserve it. Comparison captures this value as `source_revision`; the frontend hides results when revisions differ. The backend also supplies the loaded scenario name.

The frontend admits one mutation request at a time, suspends overlapping polling during mutations, discards pre-mutation snapshots, refreshes after completion and preserves drafts until applied. Applying/resetting scenarios clears stale configuration drafts. The same CSV serializer used by the export button is tested against actual API results.

The HTTP signal handler writes only a statically verified lock-free atomic flag. The main thread observes it and stops/joins the listener outside signal context; listener completion is also observed atomically so an unexpected listener exit cannot leave the main polling loop running indefinitely.
