# Audit evidence

- `demo-results.json`: actual API job, Gantt, memory, resource, metric and event data captured by `python3 scripts/audit_test.py` for all five required demonstrations.
- `comparison.json`: actual repeated, isolated four-policy comparison for the SJF workload.
- `comparison.csv`: written and validated by `node scripts/test_frontend.mjs docs/evidence/comparison.json`, using `web/js/exports.js`, the same serializer invoked by the browser button.
- `http-audit.txt`: output of actual malformed-input, demo, concurrent-client, reset, port-conflict and startup/shutdown assertions.

These are runtime evidence artifacts, not fabricated screenshots. Logical timestamps and job IDs describe their particular test run; wall timestamps and process IDs may differ on replay. Browser click/download and exact viewport visual checks remain explicitly separate.

Build/test transcripts include native-build.txt, debug-build.txt, linux-verification.txt and thread-sanitizer.txt. Source-change hashes compare the preserved baseline against the audit files; original tests and demo CSVs were not modified.

`http-thread-sanitizer.txt` records a separately instrumented real HTTP server passing the independent API audit. `docker-build.txt` records the final local image build and its 42-case test run.
