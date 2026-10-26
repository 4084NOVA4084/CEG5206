#!/usr/bin/env python3
"""Benchmark sweeps (+ perf stat on Linux) -> results/<timestamp>/

    system_info.json   CPU, caches, compiler, perf availability
    throughput.csv     one row per (sweep, algo, N, K, P, tile, merge, dist)
    bandwidth.csv      stream-max GB/s vs working-set size
    perf_counters.csv  counters per config, timed region only (Linux)
    commands.log       every command that ran

    python3 scripts/run_benchmarks.py --preset full --dram-peak-gbs 51.2
    python3 scripts/run_benchmarks.py --preset full --only scaling,perf
Plot with: python3 scripts/plot_results.py results/<timestamp>
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import io
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
EXE = ".exe" if os.name == "nt" else ""  # native Windows Python; MSYS/Cygwin resolve it themselves
BENCH = BUILD / f"bench_topk{EXE}"
BANDWIDTH = BUILD / f"bench_bandwidth{EXE}"

PERF_EVENTS = [
    "cycles", "instructions", "branches", "branch-misses",
    "L1-dcache-loads", "L1-dcache-load-misses", "LLC-loads", "LLC-load-misses",
]

SWEEPS = ["size", "k", "scaling", "weak", "tile", "dist", "bandwidth", "perf"]


def _run_text(cmd: List[str]) -> str:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def _sysctl(name: str) -> Optional[int]:
    out = _run_text(["sysctl", "-n", name]).strip()
    return int(out) if out.isdigit() else None


def detect_system() -> Dict:
    info: Dict = {
        "timestamp": dt.datetime.now().isoformat(timespec="seconds"),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "hostname": platform.node(),
        "logical_cpus": os.cpu_count(),
        "compiler": _run_text([os.environ.get("CXX", "c++"), "--version"]).splitlines()[:1],
    }
    if sys.platform.startswith("linux"):
        lscpu = _run_text(["lscpu"])
        fields = {}
        for line in lscpu.splitlines():
            if ":" in line:
                key, value = line.split(":", 1)
                fields[key.strip()] = value.strip()
        info["cpu_model"] = fields.get("Model name", platform.processor())
        info["sockets"] = fields.get("Socket(s)")
        info["threads_per_core"] = fields.get("Thread(s) per core")
        info["l1d"] = fields.get("L1d cache")
        info["l2"] = fields.get("L2 cache")
        info["l3"] = fields.get("L3 cache")
        info["numa_nodes"] = fields.get("NUMA node(s)")
        # lscpu lists logical CPUs; physical core = unique (core, socket)
        pairs = set()
        for line in _run_text(["lscpu", "-p=core,socket"]).splitlines():
            if line and not line.startswith("#"):
                pairs.add(tuple(line.split(",")[:2]))
        info["physical_cores"] = len(pairs) or os.cpu_count()
        info["cache_bytes"] = {
            level: _linux_cache_size(level) for level in ("L1d", "L2", "L3")
        }
        paranoid = Path("/proc/sys/kernel/perf_event_paranoid")
        info["perf_event_paranoid"] = paranoid.read_text().strip() if paranoid.exists() else None
        info["kernel"] = platform.release()
    elif sys.platform == "darwin":
        info["cpu_model"] = _run_text(["sysctl", "-n", "machdep.cpu.brand_string"]).strip()
        info["physical_cores"] = _sysctl("hw.physicalcpu") or os.cpu_count()
        info["performance_cores"] = _sysctl("hw.perflevel0.physicalcpu")
        info["efficiency_cores"] = _sysctl("hw.perflevel1.physicalcpu")
        info["cache_bytes"] = {
            "L1d": _sysctl("hw.perflevel0.l1dcachesize") or _sysctl("hw.l1dcachesize"),
            "L2": _sysctl("hw.perflevel0.l2cachesize") or _sysctl("hw.l2cachesize"),
            "L3": _sysctl("hw.l3cachesize"),
        }
        info["memory_bytes"] = _sysctl("hw.memsize")
    else:
        info["cpu_model"] = platform.processor()
        info["physical_cores"] = os.cpu_count()
        info["cache_bytes"] = {}
    return info


def _linux_cache_size(level: str) -> Optional[int]:
    """cpu0's view: per-core for L1d/L2, per shared instance for L3."""
    want_level = {"L1d": ("1", "Data"), "L2": ("2", "Unified"), "L3": ("3", "Unified")}[level]
    base = Path("/sys/devices/system/cpu/cpu0/cache")
    if not base.exists():
        return None
    for index in base.glob("index*"):
        try:
            lvl = (index / "level").read_text().strip()
            typ = (index / "type").read_text().strip()
            size = (index / "size").read_text().strip()
        except OSError:
            continue
        if (lvl, typ) == want_level:
            match = re.match(r"(\d+)([KMG]?)", size)
            if match:
                mult = {"": 1, "K": 1 << 10, "M": 1 << 20, "G": 1 << 30}[match.group(2)]
                return int(match.group(1)) * mult
    return None


def thread_counts(physical: int, logical: int, limit: Optional[int]) -> List[int]:
    """Powers of two up to the core count, plus the exact core and SMT counts."""
    top = min(limit, logical) if limit else logical
    counts = []
    p = 1
    while p <= min(physical, top):
        counts.append(p)
        p *= 2
    for extra in (physical, logical):
        if extra <= top and extra not in counts:
            counts.append(extra)
    return sorted(counts)


def perf_capabilities() -> Dict[str, bool]:
    caps = {"available": False, "fifo_roi": False}
    if not sys.platform.startswith("linux") or shutil.which("perf") is None:
        return caps
    probe = subprocess.run(["perf", "stat", "-x,", "-e", "instructions:u", "--", "true"],
                           capture_output=True, text=True)
    caps["available"] = probe.returncode == 0 and "<not supported>" not in probe.stderr
    if not caps["available"]:
        return caps
    with tempfile.TemporaryDirectory() as tmp:
        ctl, ack = Path(tmp, "ctl"), Path(tmp, "ack")
        os.mkfifo(ctl)
        os.mkfifo(ack)
        probe = subprocess.run(
            ["perf", "stat", "-x,", "--delay=-1", f"--control=fifo:{ctl},{ack}",
             "-e", "instructions:u", "--", "true"],
            capture_output=True, text=True, timeout=30)
        caps["fifo_roi"] = probe.returncode == 0
    return caps


def canonical_event(name: str) -> str:
    """'cpu_core/branch-misses:u/' -> 'branch-misses'"""
    name = name.strip()
    match = re.match(r"^[\w.-]+/(.+?)/?$", name)
    if match:
        name = match.group(1)
    return name.split(":")[0]


def parse_perf_csv(text: str) -> Dict[str, Dict[str, float]]:
    values: Dict[str, List[float]] = {}
    pcts: Dict[str, List[float]] = {}
    for row in csv.reader(io.StringIO(text)):
        if not row or row[0].startswith("#") or len(row) < 3:
            continue
        event = canonical_event(row[2])
        if not event:
            continue
        try:
            values.setdefault(event, []).append(float(row[0]))
        except ValueError:          # <not supported> / <not counted>
            values.setdefault(event, [])
            continue
        try:
            pcts.setdefault(event, []).append(float(row[4]))
        except (IndexError, ValueError):
            pass
    nan = float("nan")
    # hybrid CPUs print one line per PMU (cpu_core / cpu_atom)
    return {event: {"value": sum(v) if v else nan,
                    "pct_running": min(pcts[event]) if pcts.get(event) else nan}
            for event, v in values.items()}


class Runner:
    def __init__(self, args: argparse.Namespace, out_dir: Path, sysinfo: Dict):
        self.args = args
        self.out = out_dir
        self.sysinfo = sysinfo
        self.log = open(out_dir / "commands.log", "a")
        self.throughput_rows: List[Dict] = []
        self.perf_rows: List[Dict] = []
        self.failures = 0
        self.env = dict(os.environ)
        self.env.setdefault("OMP_PROC_BIND", "close")
        self.env.setdefault("OMP_PLACES", "cores")

    def _exec(self, cmd: List[str], **kw) -> subprocess.CompletedProcess:
        self.log.write(" ".join(cmd) + "\n")
        self.log.flush()
        if self.args.verbose:
            print("  $", " ".join(cmd))
        return subprocess.run(cmd, capture_output=True, text=True, env=self.env, **kw)

    @staticmethod
    def reps_for(n: int) -> int:
        # ~2^27 elements of work per config; small N would otherwise be all noise
        return max(10, min(300, (1 << 27) // max(1, n)))

    def bench(self, sweep: str, n: int, algos: str, ks, threads="1", tiles="1024",
              merges="seq", dist="uniform", extra: Optional[List[str]] = None) -> List[Dict]:
        reps = self.args.reps or self.reps_for(n)
        cmd = [str(BENCH), "--n", str(n), "--dist", dist, "--algo", algos,
               "--k", ",".join(map(str, ks)), "--threads", str(threads),
               "--tile", str(tiles), "--merge", merges, "--reps", str(reps),
               "--warmup", str(max(2, reps // 10)), "--csv-header"]
        if not self.args.no_verify:
            cmd.append("--verify")
        cmd += extra or []
        proc = self._exec(cmd)
        if proc.returncode != 0:
            self.failures += 1
            print(f"  ! bench exited with {proc.returncode}: {proc.stderr.strip()[-300:]}")
        rows = list(csv.DictReader(io.StringIO(proc.stdout)))
        for row in rows:
            row["sweep"] = sweep
        self.throughput_rows.extend(rows)
        self._write_csv("throughput.csv", self.throughput_rows)
        return rows

    def _write_csv(self, name: str, rows: List[Dict]) -> None:
        if not rows:
            return
        fields: List[str] = []
        for row in rows:
            for key in row:
                if key not in fields:
                    fields.append(key)
        with open(self.out / name, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)

    def perf_bench(self, label: str, n: int, algo: str, k: int, threads: int,
                   merge: str = "seq", tile: int = 1024, reps: int = 10) -> None:
        caps = self.sysinfo["perf"]
        events = [e + ("" if self.args.perf_kernel else ":u") for e in PERF_EVENTS]
        base = [str(BENCH), "--n", str(n), "--algo", algo, "--k", str(k),
                "--threads", str(threads), "--tile", str(tile), "--merge", merge,
                "--warmup", "1", "--csv-header"]
        mode = self.args.roi
        if mode == "auto":
            mode = "fifo" if caps["fifo_roi"] else "diff"

        with tempfile.TemporaryDirectory() as tmp:
            perf_out = Path(tmp, "perf.txt")
            if mode == "fifo":
                ctl, ack = Path(tmp, "ctl"), Path(tmp, "ack")
                os.mkfifo(ctl)
                os.mkfifo(ack)
                cmd = ["perf", "stat", "-x,", "-o", str(perf_out), "-e", ",".join(events),
                       "--delay=-1", f"--control=fifo:{ctl},{ack}", "--",
                       *base, "--reps", str(reps), "--perf-ctl", str(ctl), "--perf-ack", str(ack)]
                proc = self._exec(cmd, timeout=1800)
                failed = proc.returncode != 0
                counters = parse_perf_csv(perf_out.read_text() if perf_out.exists() else "")
                bench_rows = list(csv.DictReader(io.StringIO(proc.stdout)))
            else:
                # old perf: (warmup + R reps) - (warmup + 0 reps)
                totals = []
                bench_rows = []
                failed = False
                for r in (reps, 0):
                    cmd = ["perf", "stat", "-x,", "-o", str(perf_out), "-e", ",".join(events),
                           "--", *base, "--reps", str(r)]
                    proc = self._exec(cmd, timeout=1800)
                    failed = failed or proc.returncode != 0
                    totals.append(parse_perf_csv(perf_out.read_text() if perf_out.exists() else ""))
                    if r:
                        bench_rows = list(csv.DictReader(io.StringIO(proc.stdout)))
                counters = {}
                for event, entry in totals[0].items():
                    zero = totals[1].get(event, {"value": 0.0})["value"]
                    counters[event] = {"value": entry["value"] - zero,
                                       "pct_running": entry["pct_running"]}
            if failed:
                self.failures += 1
                print(f"  ! perf run failed ({label}): {proc.stderr.strip()[-300:]}")

        row: Dict = {"label": label, "algo": algo, "n": n, "k": k, "threads": threads,
                     "merge": merge, "tile": tile, "reps": reps, "roi": mode}
        if bench_rows:
            row["median_ms"] = bench_rows[0].get("median_ms")
            row["melem_s"] = bench_rows[0].get("melem_s")
        elements = float(n) * reps
        for event in PERF_EVENTS:
            entry = counters.get(event, {"value": float("nan"), "pct_running": float("nan")})
            row[event] = entry["value"]
            row[event + "_pct_running"] = entry["pct_running"]
            row[event + "_per_elem"] = entry["value"] / elements if elements else float("nan")
        self.perf_rows.append(row)
        self._write_csv("perf_counters.csv", self.perf_rows)


def run_sweeps(runner: Runner, args: argparse.Namespace) -> None:
    info = runner.sysinfo
    full = args.preset == "full"
    phys = int(info.get("physical_cores") or os.cpu_count() or 1)
    logical = int(info.get("logical_cpus") or phys)
    P_list = thread_counts(phys, logical, args.threads_max)
    P_max = min(phys, args.threads_max or phys)
    P_str = ",".join(map(str, P_list))

    sizes = [1 << e for e in range(15, 26)] if full else [1 << e for e in (15, 17, 19, 21, 23)]
    n_big = (32 << 20) if full else (8 << 20)
    n_mid = (8 << 20) if full else (2 << 20)
    ks_all = [8, 16, 32, 64, 128, 256, 512, 1024]
    selected = set(args.only.split(",")) if args.only else set(SWEEPS)

    def banner(name: str) -> bool:
        if name not in selected:
            return False
        print(f"== sweep: {name}")
        return True

    if banner("size"):
        for n in sizes:
            runner.bench("size", n, "heap,nth,tiled", [8, 64, 1024] if full else [64])
            runner.bench("size", n, "omp_tiled,omp_heap", [8, 64, 1024] if full else [64],
                         threads=P_max)

    if banner("k"):
        runner.bench("k", n_mid, "heap,tiled", ks_all)
        runner.bench("k", n_mid, "omp_tiled", ks_all, threads=P_max)
        if full:
            runner.bench("k", n_mid, "nth", [8, 64, 1024])

    if banner("scaling"):
        merges = "seq,two,tree" if full else "seq,tree"
        runner.bench("scaling", n_big, "omp_tiled", [64, 1024] if full else [64],
                     threads=P_str, merges=merges)
        runner.bench("scaling", n_big, "omp_heap", [64], threads=P_str)

    if full and banner("weak"):  # 4M elements per thread
        for p in P_list:
            runner.bench("weak", (4 << 20) * p, "omp_tiled", [64], threads=p)

    if banner("tile"):
        tiles = "128,256,512,1024,2048,4096,8192,16384"
        runner.bench("tile", n_mid, "tiled", [16, 64, 256] if full else [64], tiles=tiles)

    if banner("dist"):
        for dist in ("uniform", "gaussian", "ascending", "descending", "duplicates", "special"):
            runner.bench("dist", n_mid, "heap,tiled", [64], dist=dist)
            runner.bench("dist", n_mid, "omp_tiled", [64], threads=P_max, dist=dist)

    if banner("bandwidth"):
        cmd = [str(BANDWIDTH), "--threads", f"1,{P_max}" + (f",{logical}" if logical > P_max else ""),
               "--min-size", "16K", "--max-size", "512M" if full else "256M", "--csv-header"]
        proc = runner._exec(cmd, timeout=3600)
        (runner.out / "bandwidth.csv").write_text(proc.stdout)

    if banner("perf"):
        if not info["perf"]["available"]:
            print("   perf not available (needs Linux + perf + perf_event_paranoid <= 2): skipped")
        else:
            for k in ([64, 1024] if full else [64]):
                for algo, p in (("heap", 1), ("nth", 1), ("tiled", 1),
                                ("omp_tiled", 1), ("omp_tiled", P_max), ("omp_heap", P_max)):
                    label = f"{algo}_P{p}_K{k}"
                    print(f"   perf: {label}")
                    runner.perf_bench(label, n_big, algo, k, p)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", choices=["quick", "full"], default="quick")
    parser.add_argument("--only", help="comma-separated subset of: " + ",".join(SWEEPS))
    parser.add_argument("--out", type=Path, help="results directory (default results/<timestamp>)")
    parser.add_argument("--threads-max", type=int, help="cap on thread counts")
    parser.add_argument("--reps", type=int, help="override repetitions per configuration")
    parser.add_argument("--roi", choices=["auto", "fifo", "diff"], default="auto",
                        help="how perf counters are restricted to the timed region")
    parser.add_argument("--perf-kernel", action="store_true",
                        help="count kernel-mode events too (default: user-mode only, ':u')")
    parser.add_argument("--dram-peak-gbs", type=float,
                        help="theoretical DRAM bandwidth (channels x MT/s x 8 B) for plots")
    parser.add_argument("--peak-gops", type=float,
                        help="theoretical compare throughput in G/s (cores x GHz x 8 lanes x ports)"
                             " for the roofline compute roof")
    parser.add_argument("--no-build", action="store_true", help="skip `make`")
    parser.add_argument("--no-verify", action="store_true", help="skip result verification")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    if not args.no_build:
        print("== make")
        subprocess.run(["make", "-C", str(ROOT), "all"], check=True)
    for binary in (BENCH, BANDWIDTH):
        if not binary.exists():
            print(f"missing {binary}; run make first", file=sys.stderr)
            return 1

    out = args.out or ROOT / "results" / dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    out.mkdir(parents=True, exist_ok=True)

    sysinfo = detect_system()
    sysinfo["perf"] = perf_capabilities()
    sysinfo["preset"] = args.preset
    sysinfo["dram_peak_gbs"] = args.dram_peak_gbs
    sysinfo["peak_gops"] = args.peak_gops
    sysinfo["omp_env"] = {k: os.environ.get(k, d) for k, d in
                          (("OMP_PROC_BIND", "close"), ("OMP_PLACES", "cores"))}
    (out / "system_info.json").write_text(json.dumps(sysinfo, indent=2))
    print(f"== results -> {out}")
    print(f"   cpu: {sysinfo.get('cpu_model')}  physical cores: {sysinfo.get('physical_cores')}"
          f"  perf: {sysinfo['perf']}")

    runner = Runner(args, out, sysinfo)
    try:
        run_sweeps(runner, args)
    except KeyboardInterrupt:
        print("\ninterrupted; partial results kept")
    print(f"== done: {len(runner.throughput_rows)} throughput rows, "
          f"{len(runner.perf_rows)} perf rows, {runner.failures} failures")
    print(f"   plot: python3 {Path(__file__).parent / 'plot_results.py'} {out}")
    return 1 if runner.failures else 0


if __name__ == "__main__":
    sys.exit(main())
