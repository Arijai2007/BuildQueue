# BuildQueue final audit

## Verdict

**READY WITH DOCUMENTED LIMITATIONS.** The core, CLI, real HTTP API and five practical demonstrations have independently checked behavior. Complete browser interaction, download-click and exact-width visual QA remain open because browser automation was unavailable and native input controls did not reliably change the observed page. These items are not marked passed.

## Discovery and baseline

The existing source, build files, headers, tests, dashboard assets, eleven workloads and academic documentation were reviewed before production edits. The directory is not a Git repository; no Git diff was available. A source baseline and SHA-256 manifest were preserved at `/tmp/buildqueue-audit-baseline`. No reference repository was copied or changed, and no working test or demo was deleted.

The original suite passed **34/34**, and the existing HTTP/CLI smoke checks passed. The existing FCFS/SJF/Priority/RR strategy implementations, contiguous allocation policies, deterministic coordinator, persistent pool, semaphore and process execution were retained. Added regression probes then failed against the original core, demonstrating gaps beyond the earlier test suite.

## Findings and justified changes

| Finding | Evidence before correction | Correction |
|---|---|---|
| High: log write failure could leak a semaphore permit | Linux `/dev/full` regression left an active slot after reset | POSIX write failure becomes an explicit persistent-logging warning; in-memory events and resource bookkeeping continue; snapshot/UI expose the failure |
| Medium: two server instances could share one port | A second server stayed running instead of rejecting the occupied port; upstream default uses SO_REUSEPORT | Server opts into SO_REUSEADDR only; independent startup test now requires conflict exit 1 |
| Medium: exact run budget produced a false failure | A one-tick job with `runUntilDone(1)` completed then threw | Check unfinished work after the budget; align default budget with the documented engine bound |
| Medium: maximum memory capacity was unreachable | 1048576 MB rejected by a generic one-million integer guard | Validate representable signed/unsigned integers before per-field bounds |
| Medium: malformed identifiers had inconsistent status | Missing cancellation ID returned 404; overflowing path ID treated like a missing job | Required-field checks and strict numeric path parsing return 400; valid missing IDs retain 404 |
| Medium: controls could overlap or show stale state | Source review found unguarded rapid actions and pre-mutation polling results | One mutation gate, disabled pending controls, bounded requests and epoch-checked refreshes |
| Medium: comparisons and labels could outlive their workload | Source review found local-only scenario labels and incomplete invalidation | Backend scenario/revision metadata; compare source revision; invalidate on input/configuration changes |
| Medium: default arrival could expire while filling a form | Source review found arrival sampled when the form opened | Atomic opt-in `arrival_now`; dashboard defaults and CLI implicit arrivals use it; explicit schedules remain supported |
| Low: Gantt polling reset horizontal position | Source review of repeated innerHTML replacement | Preserve scroll position and bound the number of tick labels |
| Low: mobile memory-policy Apply disappeared; small text and narrow panels were difficult to use | CSS/source inspection and baseline screenshot | Keep explicit Apply at every width; increase text/touch sizes, provide navigation names, improve light contrast and narrow layout containment |
| Low: debug build could overwrite the release binary | Shared Make output target | Debug writes `.build-debug/buildqueue`; release hash preservation is checked |
| Low: signal delivery or listener failure could be unsafe across server threads | Source review of a shared volatile signal flag and an unchecked listener exit | Use a guaranteed lock-free atomic signal flag and observe listener completion; verify actual HTTP concurrency/shutdown under TSan |
| Low: process setup errors and pool construction needed safer cleanup | Source review | Report output-sink/fork errors as WorkResult; retain fork errno; join partially-created worker sets; build replacement pool before swapping |

`tests/test_audit.cpp` adds exact-budget, capacity, identifier, atomic-arrival, revision, repeated-reset, timeout/reaping and Linux disk-full regressions. Original tests remain intact. `scripts/audit_test.py` adds actual HTTP error-schema, five-demo, concurrent-client, repeated lifecycle and export evidence. `scripts/test_frontend.mjs` checks request gating, HTML escaping, rendering data and the actual comparison serializer; these are pure JavaScript tests, not a browser substitute.

## Files changed

- `include/BuildQueueEngine.hpp`, `src/BuildQueueEngine.cpp`: tick-budget fix, atomic current arrival, scenario/revision metadata and safe worker replacement.
- `include/Logger.hpp`, `src/Logger.cpp`: non-interrupting disk-write diagnostics and exposed persistence status.
- `src/JobManager.cpp`, `src/Api.cpp`, `src/HttpServer.cpp`: integer/identifier/schema validation, JSON transport errors and exclusive listening.
- `src/WorkerManager.cpp`, `src/CLI.cpp`, `Makefile`: process/pool failure handling, current-arrival CLI behavior and isolated debug binary.
- `web/index.html`, `web/css/styles.css`, `web/js/{app,api,charts,exports}.js`: coherent controls, readable responsive presentation and export serialization.
- `tests/test_audit.cpp`, `scripts/{audit_test.py,test_frontend.mjs,smoke_test.py,verify_linux.sh}`: additional independent verification.
- README, design, architecture, API, testing, demo, viva and status documents: final behavior, recovery commands and explicit limitations.
- `docs/evidence/`: actual API snapshots, comparisons and execution results.

## Demonstration evidence

| Practical demo | Exact method | Actual observed state | Result |
|---|---|---|---|
| First things first | Load fcfs; run to completion | Tick 9; waits 0/3/5; turnaround 4/6/7; averages 8/3 and 17/3 | PASS |
| A fair share | Load round_robin; Step twice | Tick 2; first job READY with 3 remaining; 192 MB retained; execution permit released | PASS |
| Space, scattered | Load fragmentation; Step three times | 400 MB request WAITING, 580 MB free, largest hole 300 MB; actual memory blocks and denial event recorded | PASS |
| Controlled concurrency | Load synchronization; Step then finish | Four logical lanes, two holders and two blocked at tick 1; peak holders 2; all five complete; atomic counter 18 | PASS |
| The shortest path | Load sjf; compare twice; parse exports | Identical four-policy results and unchanged parent state; JSON/CSV metrics match | PASS |

Actual source data is in [demo-results.json](evidence/demo-results.json), [comparison.json](evidence/comparison.json), and [comparison.csv](evidence/comparison.csv). CSV is written by the same JavaScript serializer used by the dashboard export control. Browser click/download delivery is NOT VERIFIED.

## Browser and responsive quality gate

The baseline dashboard rendered in native Chrome and was visually inspected. The browser extension host was unavailable. Attempts to reload, navigate and open responsive inspection via native controls returned unchanged/stale states or a user-change refusal. The refined assets were served and checked through HTTP and JavaScript tests; no fabricated screenshots or browser coverage claims are included.

| Width | Exact viewport visual/overflow check | Interaction check |
|---|---|---|
| 1440 px | NOT VERIFIED | NOT VERIFIED |
| 1024 px | NOT VERIFIED | NOT VERIFIED |
| 768 px | NOT VERIFIED | NOT VERIFIED |
| 390 px | NOT VERIFIED | NOT VERIFIED |
| 320 px | NOT VERIFIED | NOT VERIFIED |

Before the practical, exercise Run/Pause/Step, Reset, policy/quantum, memory Apply, configuration, submission/cancellation, comparison, both downloads and theme at the actual presentation browser. Inspect horizontal page overflow and keyboard access at all five widths. Existing APIs and the pure rendering/export logic have tests; this does not establish every-control browser success.

## Metadata verification

The official [JIIT B.Tech CSE curriculum](https://www.jiit.ac.in/uploads/B_Tech_CSE_2024_Curricula_Electives_Proficiency_Course_Outline_30b6882387.pdf) lists **24B15CS312 — Operating Systems Lab**. No conflicting course code was found in the project. The institution, three student names/enrollment numbers, F1 batch and Dr. Ambalika Sarkar match the supplied project brief and existing documents. No signed institutional submission sheet or enrollment roster was available; those personal/submission details are preserved as supplied, not independently certified.

## Remaining issues and practical recovery

| Severity | Remaining limitation | Reproduction / action |
|---|---|---|
| Medium | Final browser input, five viewport sizes, theme appearance and download-click QA incomplete | Open the dashboard and follow the manual quality gate above |
| Low | No priority aging or live-process preemption | Continuous high-priority arrivals can starve lower priorities; live mode requires a non-preemptive policy |
| Low | Persistence is an event log, not queue restoration | Restart creates an empty experiment; reload a demo |
| Low | Disk logging needs restart after a write failure | Restore writable storage, retain warning/evidence, restart and reload the workload |
| Low | Platform sanitizer runtime restrictions | Use the documented compatible Linux helpers; macOS runtime history is qualified in TESTING.md |
| Administrative | Team/supervisor details lack an independent signed source | Check the supplied identity table against the final university submission form |

No unresolved critical crash or hang was observed in the verified paths. Process setup failure branches were reviewed; timeout, nonzero process exit, reaping and cleanup were exercised. Forced OS-level fork/thread creation exhaustion was not induced on the user's host.

## Final checklist

- [x] Existing core/tests/demos preserved; findings backed by regressions or source inspection.
- [x] Scheduling and contiguous allocation numerically checked; semaphore limit asserted.
- [x] CLI/API operations, all five backend demos, exports and repeated lifecycle tested.
- [x] Documentation distinguishes logical scheduling from real threads/processes.
- [x] Institution/course source checked; supplied team details preserved.
- [x] Evidence and remaining limitations disclosed.
- [ ] Every visible control verified through a reliable browser session.
- [ ] All five exact viewport widths visually checked.
- [ ] Browser export downloads and complete light-theme appearance verified.

Actual final compiler/sanitizer results and command transcripts are recorded in [TESTING.md](TESTING.md).
