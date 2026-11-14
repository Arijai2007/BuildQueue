#!/usr/bin/env python3
"""End-to-end HTTP, assets, CLI and shutdown checks. Python is test-only."""
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "buildqueue"

def check(condition, message):
    if not condition:
        raise AssertionError(message)
    print("PASS", message, flush=True)

with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
base = f"http://127.0.0.1:{port}"
process = subprocess.Popen([str(EXE), "--serve", "--port", str(port), "--root", str(ROOT)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def request(path, data=None, expected=200, raw=None, headers=None):
    body = raw if raw is not None else json.dumps(data).encode() if data is not None else None
    hdr = {"Content-Type": "application/json"} if body is not None else {}
    hdr.update(headers or {})
    req = urllib.request.Request(base + path, data=body, headers=hdr)
    try:
        response = urllib.request.urlopen(req, timeout=15)
    except urllib.error.HTTPError as error:
        response = error
    payload = response.read()
    check(response.status == expected, f"HTTP {expected} {path}")
    return json.loads(payload).get("data") if path.startswith("/api/") else payload.decode()

try:
    for attempt in range(100):
        try:
            with urllib.request.urlopen(base + "/api/health", timeout=1):
                break
        except (OSError, urllib.error.URLError):
            if process.poll() is not None:
                raise RuntimeError(process.stdout.read().decode())
            time.sleep(0.05)
    else:
        raise RuntimeError("HTTP startup timed out")
    check("BuildQueue Control Center" in request("/"), "Dashboard title is served")
    for path in ["/css/styles.css", "/js/app.js", "/js/api.js", "/js/charts.js", "/js/components.js", "/js/exports.js", "/assets/favicon.svg"]:
        check(len(request(path)) > 20, "Asset present " + path)
    for key in ["health", "jobs", "memory", "resources", "stats", "gantt", "log", "config", "demos", "snapshot"]:
        request("/api/" + key)
    request("/api/submit", raw=b"{", expected=400)
    request("/api/submit", {"burst": 1.5}, expected=400)
    request("/api/submit", {"type": "process"}, expected=400)
    request("/api/submit", {}, expected=403, headers={"Origin": "https://example.com"})
    request("/api/submit", raw=b"{}", expected=415, headers={"Content-Type": "text/plain"})
    request("/api/workers", {"workers": 0}, expected=400)
    request("/api/load-demo", {"name": "../../etc/passwd"}, expected=400)
    request("/api/load-demo", {"name": "fcfs"})
    request("/api/step", {})
    check(request("/api/stats")["tick"] == 1, "Step advances exactly one logical tick")
    before = request("/api/snapshot")
    comparison = request("/api/compare", {})
    check(len(comparison["results"]) == 4, "Comparison runs all four strategies")
    check(abs(comparison["results"][0]["stats"]["avg_wait"] - 8 / 3) < 1e-10, "HTTP FCFS metrics match fixture")
    check(before == request("/api/snapshot"), "Comparison preserves original experiment")
    request("/api/config", {"delay_ms": 5})
    request("/api/run", {})
    time.sleep(0.02)
    request("/api/pause", {})
    tick = request("/api/stats")["tick"]
    time.sleep(0.05)
    check(tick == request("/api/stats")["tick"], "Pause freezes logical clock")
    request("/api/resume", {})
    for attempt in range(100):
        stats = request("/api/stats")
        if stats["completed"] == 3:
            break
        time.sleep(0.02)
    check(stats["completed"] == 3, "Async run and resume finish workload")
    check(request("/api/memory")["used"] == 0, "Completion reclaims memory")
    for name in ["memory_first_fit", "memory_best_fit", "memory_worst_fit", "fragmentation", "synchronization"]:
        request("/api/load-demo", {"name": name})
        for _ in range(3 if name != "synchronization" else 1):
            request("/api/step", {})
        state = request("/api/snapshot")
        if name == "fragmentation":
            check(state["jobs"][4]["state"] == "WAITING" and state["memory"]["free"] == 580, "Fragmentation blocks 400 MB despite 580 MB free")
        if name == "synchronization":
            check(state["resources"]["active_slots"] == 2 and state["stats"]["waiting"] == 2, "Four workers respect two semaphore slots")
    request("/api/reset", {})
    request("/api/config", {"workers": 2, "mode": "live"})
    request("/api/submit", {"name": "Controlled process", "type": "process", "burst": 1}, expected=201)
    request("/api/step", {})
    check(request("/api/jobs")[0]["process_exit"] == 0, "Live process completes through HTTP")
    request("/api/reset", {})
    request("/api/config", {"mode": "simulation"})
    job = request("/api/submit", {"name": "Cancel me", "arrival": 4}, expected=201)
    request("/api/cancel", {"id": job["id"]})
    check(request(f"/api/jobs/{job['id']}")["state"] == "CANCELLED", "Cancellation visible through job endpoint")
    request("/api/cancel", {"id": job["id"]}, expected=400)
    request("/api/memory-policy", {"policy": "best"})
    request("/api/policy", {"policy": "rr", "quantum": 3})
    request("/api/clear-log", {})
    check(request("/api/log") == [], "Log view clearing works")
finally:
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise RuntimeError("Server failed to shut down cleanly")
check(process.returncode == 0, "SIGTERM shuts down server and workers cleanly")
for policy in ["fcfs", "sjf", "priority", "rr"]:
    result = subprocess.run([str(EXE), "--demo", "fcfs", "--policy", policy, "--json"], cwd=ROOT, capture_output=True, text=True, timeout=10, check=True)
    check(json.loads(result.stdout)["stats"]["completed"] == 3, "One-shot CLI policy " + policy)
cli = subprocess.run([str(EXE)], input='demo fcfs\nstep\nstatus 1\ncancel 1\nfinish\ngantt\nmemmap\nexit\n', cwd=ROOT, capture_output=True, text=True, timeout=10, check=True)
check('"remaining": 3' in cli.stdout and "Invalid transition" in cli.stdout and "J1 [0–4]" in cli.stdout, "Interactive CLI status, cancellation error, finish, Gantt and memory")
bad = subprocess.run([str(EXE), "--quantum", "2x"], cwd=ROOT, capture_output=True, text=True, timeout=10)
check(bad.returncode == 1, "Malformed one-shot options are rejected")
print("All end-to-end smoke checks passed.")
