# BuildQueue verification record

This record includes the independent final audit. The original implementation passed 34 cases; the expanded suite keeps every original test and adds targeted regressions. Browser observations and automated API/logic checks are distinguished below.

## Environments

- Host: macOS ARM64, Apple Clang 17.0.0; C++17, `-Wall -Wextra -Wpedantic`, pthreads. Make and native CLI/HTTP runs are separate from Linux verification.
- Target: Debian Bookworm Linux ARM64 in Docker Desktop, GCC/G++ 12.2.0, CMake 3.25.1 and Make. Source was copied into the container, and fresh Linux objects were built; host Mach-O objects are not reused.
- Approved offline dependencies: cpp-httplib 0.18.3 and nlohmann/json 3.11.3, with original MIT notices. Application runs have no network dependency beyond local HTTP.

## Prescribed lab tests

`make test` runs a self-contained C++ runner and prints a PASS/FAIL line per test. It returns nonzero if any assertion fails. The expanded suite contains **41 portable test cases and one Linux-only `/dev/full` case**, with all ten synopsis test IDs explicitly present.

| ID | Test | Assertions |
|---|---|---|
| T1 | Initialization | Empty queue, tick zero, one worker, two slots, 1024 MB free |
| T2 | Submission | Arrival, priority, burst and memory preserved; reached jobs READY, future jobs NEW |
| T3 | FCFS | Order J1/J2/J3; completions 4/7/9; waits 0/3/5; exact averages 8/3 and 17/3 |
| T4 | SJF | Shortest original eligible burst; later short arrival does not interrupt |
| T5 | Priority | Numeric ascending priority, arrival/ID stable ties |
| T6 | RR | Quantum two, cyclic order, remaining burst, release/retention, arrivals-before-requeue |
| T7 | Memory | Actual chosen address under First/Best/Worst Fit: 0/280/724 MB |
| T8 | Synchronization | Eight real threads, semaphore limit two, 1600 protected increments without loss |
| T9 | Insufficient memory | Blocked job performs no work, explicit reason, resumes after three intervals |
| T10 | Release | Completion and between-quanta cancellation reclaim memory and permit reuse |

Additional cases verify invalid input transactionality, cancellation of running/completed/missing/repeated IDs, unique IDs across reset, future idle intervals, state-transition rejection, coalescing, double free, real fragmentation, all eleven demos, safe resize, historical CPU capacity after resize, worker queue shutdown, deterministic replay, turnaround conservation, comparison isolation, 4-worker/2-slot contention, concurrent status reads, independent concurrent allocator calls, RR progress under memory pressure, process errors and cleanup, real POSIX execution, JSON/API validation, Gantt slice boundaries surviving log clearing, and correct resource-blocked accounting even while all CPU lanes are occupied.

T3 prints expected and actual unrounded values:

```text
Expected wait 8/3 = 2.66667; actual 2.6666666666666665
Expected turnaround 17/3 = 5.66667; actual 5.666666666666667
```

Floating comparisons use a 1e-10 tolerance on full-precision internal values; tests do not compare rounded display strings. Gantt, completion and waiting assertions use exact integer ticks.

## Observed build and test results

| Check | Observed result |
|---|---|
| Native Make build, warnings enabled | Passed; no final compiler warnings |
| Native C++ suite | **41/41 PASS** |
| Final Debian Make build and suite | **42/42 PASS**, no compiler warnings |
| Final Debian CMake Debug + CTest | **1/1 CTest PASS**, executing all 42 Linux C++ cases |
| Native and Linux real HTTP/CLI smoke script | **Passed**: all checks, assets and shutdown |
| JavaScript syntax, four ES modules | Passed `node --check`; Node is a verification tool only |
| Final Linux ASan + UBSan | **42/42 PASS**, no sanitizer diagnostics |
| Final Linux TSan with runtime-compatible container policy | **42/42 PASS**, no race diagnostics |

Final audit commands use `sh scripts/verify_linux.sh` and `sh scripts/verify_tsan_linux.sh`. CTest has one lab_tests entry invoking the complete C++ runner. The one Linux-only test writes to `/dev/full` to prove that a failed disk log cannot leak a permit. Native and Linux counts therefore intentionally differ by one.

## End-to-end smoke checks

`python3 scripts/smoke_test.py` starts its own temporary loopback server, sends real HTTP requests, terminates it with SIGTERM, and checks exit zero. It exercises every required GET and POST endpoint, asset delivery, invalid/malformed JSON, fractional numbers, resource bounds, blocked cross-origin mutations, required JSON content type, traversal-like demo rejection, one-tick stepping, pause clock stability, resume/completion, exact FCFS metrics through HTTP, comparison state isolation, all allocator demos, fragmentation WAITING, semaphore limits, live process completion, cancellation, view clearing, and four one-shot CLI policies. It also drives the interactive CLI via stdin to check status, invalid running cancellation, finish, Gantt and memory output.

The script uses Python's standard library only. It requires permission to bind a temporary localhost port on restricted hosts. No test messages or data are sent to outside services.

## Sanitizers and runtime limitations

AddressSanitizer and UndefinedBehaviorSanitizer are compiled together in `.build-asan`. ThreadSanitizer is separate in `.build-tsan`; it is never linked with ASan. `BQ_ASAN` and `BQ_TSAN` are mutually exclusive CMake options.

In the earlier implementation session on this macOS host, the ASan test binary and a minimal standalone atomic/thread probe stalled before producing output; both were stopped. The TSan test binary and minimal probe exited with signal 11 / exit 139 without a useful diagnostic, including a rerun outside the sandbox. Those host runs were not repeated in the final audit and are **not passes**, and their behavior was not attributed to a source race without evidence. Linux sanitizer results provide the actual verification.

Docker's default seccomp policy initially prevented Linux TSan's `personality(ADDR_NO_RANDOMIZE)` setup, producing a runtime CHECK failure before tests. A separate disposable container with `--network none`, a read-only source mount and `--security-opt seccomp=unconfined` allowed the runtime to initialize and the tests to pass. The application requires no such setting; this is confined to the sanitizer verification container. The exact command is preserved in `scripts/verify_tsan_linux.sh`.

## Browser observations and remaining visual coverage

The browser-extension host was unavailable. Native Chrome accessibility and screenshot tools successfully showed the actual Control Center, coherent empty state, eleven-scenario modal, loaded Round Robin workload, and refreshed tick-2 timeline with 192 MB reserved and 100% logical CPU occupancy. A screenshot inspection confirmed the dark overview layout and locally served assets. No fake chart data or fabricated screenshot artifact was used.

Native browser control later reported `noWindowsAvailable`, and stale/changed UI states prevented dependable completion of every button interaction. Therefore full desktop/mobile visual QA, every-control browser tests, theme appearance at all breakpoints and browser export-download verification remain **partially unverified**. CSS contains responsive breakpoints and light-mode rules; the backing operations and export data were checked in code and through real API requests, but source presence is not described as full visual verification.

## Reproduce

```sh
make -j4
make test
python3 scripts/smoke_test.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
make asan
make tsan

# Linux validation from a host with Docker Desktop:
docker build -t buildqueue-lab .
sh scripts/verify_linux.sh
sh scripts/verify_tsan_linux.sh
```

For native sanitizer support diagnosis, compile `scripts/sanitizer_probe.cpp` with the relevant sanitizer independently of BuildQueue. Never report a successful compile as a successful sanitizer test run.

## Independent audit additions

- Baseline: clean native build, 34/34 original tests, and original smoke suite passed. New probes then reproduced tick-budget, max-capacity and ID-validation defects on native and Linux builds; the Linux disk-full probe also reproduced a permit leak. The new port-conflict test found shared-port listening and passed after the socket option correction.
- Native clean audit: `make clean && make -j4 all test`, **41/41 PASS**.
- Transport/demonstrations: `python3 scripts/audit_test.py`, **PASS**, including three additional startup/shutdown cycles, twelve HTTP reset cycles, 24 concurrent submissions and twelve coherent concurrent snapshots. Actual assertion output is in `evidence/http-audit.txt`.
- Export/render/request logic: `node scripts/test_frontend.mjs docs/evidence/comparison.json`, **5/5 PASS**. This validates the real frontend CSV serializer, gate recovery, escaping, Gantt dispatch/idle/historical lanes/scroll and memory owner/offset data. Node is developer verification only.
- Process timeout/reaping: a temporary FIFO blocks the fixed checksum input; the three-second timeout returns exit 124, resources are free, and a subsequent waitpid reports ECHILD. The fixture is removed.
- Metadata: the official JIIT curriculum confirms course code 24B15CS312. Team and supervisor details match the supplied brief; no independent signed roster was provided.
- Final browser input/viewport QA: **NOT VERIFIED** for 1440, 1024, 768, 390 and 320 px. Baseline native Chrome rendering was observed, but native actions did not reliably update observations. The refined frontend was verified through HTTP and pure logic tests, not reported as complete browser coverage.

See AUDIT_REPORT.md for all findings, file changes, five-demo observed results and the final quality gate.

## Final audit command results

| Command / method | Actual result | Evidence |
|---|---|---|
| `make clean && make -j4 all test` | PASS; 41/41 portable cases; no warnings | evidence/native-build.txt |
| `make -j4 debug` | PASS; 41/41 cases; isolated executable; release hash unchanged | evidence/debug-build.txt |
| `sh scripts/verify_linux.sh` | PASS; GCC Make 42/42, both HTTP suites, CMake/CTest 1/1 and ASan/UBSan 42/42 | evidence/linux-verification.txt |
| `sh scripts/verify_tsan_linux.sh` | PASS; separate TSan 42/42; no race report | evidence/thread-sanitizer.txt |
| `python3 scripts/smoke_test.py` | PASS on native and Linux | evidence/linux-verification.txt |
| `python3 scripts/audit_test.py` | PASS; actual demo/export/concurrent/lifecycle assertions | evidence/http-audit.txt |
| `node scripts/test_frontend.mjs docs/evidence/comparison.json` | PASS; 5/5 pure JS checks | evidence/frontend-logic.txt |
| Native Chrome baseline render | PASS, observed screenshot and accessibility state | No saved screenshot claimed |
| Final browser controls, console/network panel, downloads, five exact widths | NOT VERIFIED | Browser access host unavailable and native input unreliable |

ASan/UBSan and TSan remain separate builds. The complete final C++ suite includes all original T1–T10 tests, seven additional portable audit cases and one Linux-specific disk-full regression. No source or runtime test reported an unresolved critical failure in the exercised paths. Browser coverage and forced fork/thread-creation exhaustion are not inferred from those passes.

## HTTP transport sanitizer follow-up

The actual server executable was separately instrumented with TSan, including cpp-httplib, CLI and main, using `cmake -S . -B /tmp/bq-http-tsan -DBQ_TSAN=ON -DCMAKE_BUILD_TYPE=Debug` and `cmake --build /tmp/bq-http-tsan --target buildqueue -j4`. `python3 scripts/audit_test.py /tmp/bq-http-tsan/buildqueue` then passed all HTTP/concurrent/demo/reset/port-conflict/startup/shutdown assertions in the compatible network-disabled Linux container, without a TSan race diagnostic. See `evidence/http-thread-sanitizer.txt`.

The final `docker build -t buildqueue-lab .` also exited zero and ran all 42 Linux C++ cases against the final source, including the lock-free signal/listener correction. The final native transport audit passed after that correction. `evidence/docker-build.txt` records the actual build.
