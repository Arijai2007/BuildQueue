# BuildQueue demonstration guide

**Jaypee Institute of Information Technology · Operating Systems Lab · 24B15CS312 · PBL**  
Team: Arihant Jain (992401030014), Yuvraj Rana (992401030021), Siddharth Singh (992401030026), F1  
Submitted to Dr. Ambalika Sarkar

## A reproducible four-minute walkthrough

Build first with `make -j4 && make test`, then start `./buildqueue --serve --port 8080` and open http://127.0.0.1:8080. Begin with the dashboard at Overview. No Internet connection is needed.

| Time | Action | Explain and observe |
|---|---|---|
| 0:00–0:40 | Load demo → **First things first**; Step once, then Run | Memory and a build slot are acquired before J1 executes. At completion the Gantt is 0–4, 4–7, 7–9. READY wait average is 2.67 and CPU occupancy 100%. |
| 0:40–1:20 | Load demo → **A fair share**; Step twice; inspect CPU Scheduler, then Run | Two-tick quantum requeues J1. Its memory stays allocated while the slot is released. Job arriving at tick 2 is enqueued before the expired job. Seven dispatches and four preemptions for this CSV. |
| 1:20–2:10 | Load demo → **Space, scattered**; Step exactly three times; open Memory Manager | After two intervals A and C finish, creating holes of 180,100,300 MB. At tick 3 the 400 MB job is WAITING despite 580 MB free. Largest free block is only 300 MB; external fragmentation indicator is about 48.28%. Run to show eventual admission and complete coalescing. |
| 2:10–2:50 | Load demo → **Controlled concurrency**; Step once; open Resource Monitor | Four worker lanes, only two active jobs, two blocked jobs. Semaphore invariant never exceeds two slots. Run to completion: shared counter and busy-worker ticks both equal 18, and all memory is free. |
| 2:50–3:35 | Load demo → **The shortest path**; open Algorithm Comparison; Run comparison | Four fresh engine runs use the same four inputs and resources. Compare waiting, response and turnaround; export CSV or JSON. Current experiment stays unchanged. No policy is universally best. |
| 3:35–4:00 | Open Event Logs and Project Information | Show logical versus wall timestamps, resource releases, original architecture, team and course identity. Explain Linux still owns real kernel scheduling. |

Use **Pause** before a detailed explanation. Reduce visualization delay on CPU Scheduler to speed up playback. Step works only while paused. Loading a demo resets the experiment but preserves globally unique IDs: refer to jobs by name or use the IDs currently displayed.

## CSV schema

```csv
name,arrival,burst,priority,memory,type
Compile core,0,4,3,128,simulated
```

Six fields are required; names must not contain commas. Unix/Windows line endings and `#` comment lines are accepted. Integer fields must parse completely. File imports append only after validating every row; `demo NAME` replaces jobs and applies the scenario configuration. No timed manifest is necessary: future arrivals and different durations produce the fragmentation/deallocation behavior naturally.

## All scenario outcomes

| File | Auto configuration | Expected observation |
|---|---|---|
| `fcfs.csv` | FCFS, 1 worker, 2 slots, 1024 MB | Arrival 0/1/2, bursts 4/3/2; waits 0/3/5, completions 4/7/9, turnaround 4/6/7. |
| `sjf.csv` | SJF, 1 worker | Lint 0–2, Quick check 2–3, Compile module 3–7, Full rebuild 7–14. New one-tick job at t1 does not interrupt Lint. |
| `priority.csv` | Priority, 1 worker | Critical patch → Security tests → Regular tests → Documentation. Equal priorities use arrival then ID. |
| `round_robin.csv` | RR, q2, 1 worker | Engine 0–2 → Interface 2–4 → Tests 4–6 → Engine 6–8 → Interface 8–10 → Tests 10–11 → Engine 11–12. |
| `memory_first_fit.csv` | First Fit, 4 workers/4 slots | At tick 3 probe owns `[0,90)` MB. |
| `memory_best_fit.csv` | Best Fit, 4 workers/4 slots | At tick 3 probe owns `[280,370)` MB. |
| `memory_worst_fit.csv` | Worst Fit, 4 workers/4 slots | At tick 3 probe owns `[724,814)` MB. |
| `fragmentation.csv` | FCFS, First Fit, 4 workers/4 slots | At tick 2 free holes `[0,180)`, `[280,380)`, `[724,1024)`; at tick 3 the 400 MB job is blocked. Long partitions finish at t8; it then runs t8–11. |
| `memory_exhaustion.csv` | FCFS, 4 workers/4 slots | Large build uses 768 MB, small check uses 128 MB. 512 MB Large test waits five intervals, then runs t5–8. |
| `synchronization.csv` | FCFS, 4 workers/2 slots | A and B run t0–4; C and D t4–8; E t8–10. Busy ticks/counter 18; CPU occupancy 18/(4×10)=45%. |
| `mixed_workload.csv` | RR q2, 3 workers/2 slots | Six jobs complete with both memory and slot denials. Identical input/configuration produces identical normalized trace and metrics; every reservation is reclaimed. |

First/Best/Worst Fit all begin with A=180 MB, B=100 MB, C=100 MB, D=344 MB. A and C run for two ticks, B and D for eight. The initial unused tail is 300 MB. The 90 MB probe arrives at tick 2, when all three holes are available. The 400 MB fragmentation workload substitutes a larger probe so allocation fails for a real reason.

## CLI equivalent

```text
demo fcfs
finish
gantt
demo rr
step
step
memmap
finish
demo fragmentation
step
step
step
list
memmap
finish
demo synchronization
step
status
finish
demo sjf
compare
exit
```

For a live process, reset, select FCFS, switch to live mode, and submit `--type process --burst 1`. Run `finish`, then `status <returned-id>` and `log` to show a real child PID, exit zero and measured wall milliseconds. This invokes only the supplied checksum fixture and does not suspend or preempt the child.

## Observed independent audit evidence

The five demonstration sequences were replayed through real HTTP requests, and the actual jobs, metrics, Gantt intervals, memory blocks, resources and event stream are saved in [evidence/demo-results.json](evidence/demo-results.json). All five backend demonstrations passed. [comparison.json](evidence/comparison.json) contains actual isolated policy runs; `node scripts/test_frontend.mjs docs/evidence/comparison.json` validates and writes the CSV using the frontend export serializer. Browser download-click verification is separate and remains open.

| Demonstration | Actual observation | API result | Browser interaction status |
|---|---|---|---|
| First things first | Tick 9, 3 completed; waits 0/3/5; turnaround 4/6/7 | PASS | NOT VERIFIED in final browser controls |
| A fair share | Tick 2, first job READY with 3 remaining; 192 MB retained; slot released | PASS | NOT VERIFIED |
| Space, scattered | Tick 3, 400 MB job WAITING; 580 MB free; largest hole 300 MB | PASS | NOT VERIFIED |
| Controlled concurrency | At tick 1, 2 permit holders and 2 blocked; all 5 finish at tick 10; counter 18 | PASS | NOT VERIFIED |
| The shortest path | Two comparisons identical; parent state unchanged; four policies; exports parsed and checked | PASS | Browser download NOT VERIFIED |

## Recovery during the practical

1. If the page is disconnected, check the terminal is still running `./buildqueue --serve --port 8080` and visit `http://127.0.0.1:8080/api/health`.
2. If the port is occupied, stop the previous instance with Ctrl+C or start `./buildqueue --serve --port 8081` and use `http://127.0.0.1:8081`.
3. Reload the browser to refresh scripts. Load the required demo again; demo loading safely resets reservations and counters.
4. If a resource change is rejected, pause, then finish or reset jobs before resizing. Choose FCFS, reset and select live mode for process jobs.
5. If the browser is unavailable, use the documented CLI sequence. `demo fragmentation`, three `step` commands, `list`, and `memmap` demonstrate the same actual engine state.
6. Before presenting, manually exercise Run/Pause/Step, configuration forms, submission/cancellation, theme and export downloads, and review the five viewport widths listed in AUDIT_REPORT.md.
