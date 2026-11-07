#!/usr/bin/env python3
"""Optimisation ladder: what each optimisation adds, one change at a time.

    python3 scripts/run_ablation.py [results/<run>] [--rounds 3]    default: the newest run
    python3 scripts/run_ablation.py results/<run> --report-only     rebuild ladder.md and the figure

Writes <run>/ablation/:
    ablation.csv       one row per configuration and round
    ladder.md          the ladder, the filter x merge table and the tiling control, for each K
    system_info.json   the machine and source at the time of this run
    commands.log       every command that ran

Rounds are interleaved, so slow drift hits every configuration alike. Each round's value is that
configuration's median over its repetitions; the tables give the median and the range of rounds.

K - 1 is not a multiple of 8, so it takes simd_bitonic_merge_8's scalar path with the same filter.
That isolates the merge. The filter is isolated by the ablation builds of Member C's engine, which
differ only in the block filter (TOPK_ABLATE_FILTER, make bench-ablation).
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Sequence

import run_benchmarks as harness

ROOT = harness.ROOT
BUILD = harness.BUILD
DEFAULT_TILE = 1024
# Without a filter every block is merged, K / 8 chunk merges each. At K >= 256 one repetition
# takes seconds, so those rows get fewer repetitions.
SLOW_REPS = 3

# filter name in the results -> (binary, the filter= value that binary prints on stderr)
BUILDS = {"vector": ("bench_topk", 0), "none": ("bench_topk_nofilter", 1),
          "scalar": ("bench_topk_scalarfilter", 2)}


class Step(NamedTuple):
    key: str
    label: str
    filter: str
    algo: str
    k: int
    threads: int
    tile: int
    ladder: int  # 1 = a step of the ladder, 0 = a control


def parse_size(text: str) -> int:
    match = re.fullmatch(r"(\d+)([KMG]?)", text)
    if not match:
        raise SystemExit(f"bad size: {text}")
    return int(match[1]) << {"": 0, "K": 10, "M": 20, "G": 30}[match[2]]


def steps_for(k: int, threads: Sequence[int], n: int) -> List[Step]:
    tiled = dict(algo="tiled", threads=1, tile=DEFAULT_TILE)
    steps = [
        Step("heap", "Min-heap", "vector", "heap", k, 1, 0, 1),
        Step("scalar", "Hand-written scalar", "vector", "scalar", k, 1, 0, 1),
        Step("no_filter", "Tiled, no filter", "none", k=k, ladder=1, **tiled),
        Step("scalar_filter", "Tiled, scalar filter", "scalar", k=k, ladder=1, **tiled),
        Step("vector_filter", "Tiled, vector filter", "vector", k=k, ladder=1, **tiled),
    ]
    steps += [Step(f"omp_{p}", f"OpenMP tiled, P = {p}", "vector", "omp_tiled", k, p, DEFAULT_TILE, 1)
              for p in threads]
    steps += [
        Step("no_filter_scalar_merge", "No filter, scalar merge", "none", k=k - 1, ladder=0, **tiled),
        Step("scalar_filter_scalar_merge", "Scalar filter, scalar merge", "scalar", k=k - 1, ladder=0,
             **tiled),
        Step("vector_filter_scalar_merge", "Vector filter, scalar merge", "vector", k=k - 1, ladder=0,
             **tiled),
        Step("no_tiling", "Vector filter, B = N", "vector", "tiled", k, 1, n, 0),
    ]
    return steps


def run_step(step: Step, target_k: int, n: int, reps: int, env: Dict, log) -> Dict:
    binary, filter_id = BUILDS[step.filter]
    slow = step.filter == "none" and step.k % 8 == 0 and step.k >= 256
    count = min(reps, SLOW_REPS) if slow else reps
    cmd = [str(BUILD / binary), "--n", str(n), "--dist", "uniform", "--algo", step.algo,
           "--k", str(step.k), "--threads", str(step.threads), "--tile", str(step.tile or DEFAULT_TILE),
           "--merge", "seq", "--reps", str(count), "--warmup", "1" if slow else "2", "--verify",
           "--csv-header"]
    log.write(" ".join(cmd) + "\n")
    log.flush()
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        raise SystemExit(f"{step.label} failed: {proc.stderr.strip()[-300:]}")
    banner = re.search(r"avx2=(\d) filter=(\d)", proc.stderr)
    if not banner or int(banner[2]) != filter_id:
        raise SystemExit(f"{binary} is not the {step.filter} build: {proc.stderr[:120].strip()}")
    row = next(csv.DictReader(io.StringIO(proc.stdout)))
    return {**row, "step": step.key, "label": step.label, "filter": step.filter, "ladder": step.ladder,
            "target_k": target_k, "avx2": banner[1]}


def measure(args: argparse.Namespace, out: Path, n: int) -> None:
    env = dict(os.environ)
    env.setdefault("OMP_PROC_BIND", "close")
    env.setdefault("OMP_PLACES", "cores")
    ks = [int(k) for k in args.k.split(",")]
    threads = [int(p) for p in args.threads.split(",")]
    plan = [(k, step) for k in ks for step in steps_for(k, threads, n)]
    rows: List[Dict] = []
    with open(out / "commands.log", "a") as log:
        for number in range(1, args.rounds + 1):
            print(f"== round {number} of {args.rounds}: {len(plan)} configurations")
            for k, step in plan:
                row = run_step(step, k, n, args.reps, env, log)
                row["round"] = number
                rows.append(row)
                print(f"   K = {k:<5} {step.label:<32} {float(row['median_ms']):10.3f} ms")
                write_csv(out / "ablation.csv", rows)


def write_csv(path: Path, rows: Sequence[Dict]) -> None:
    fields: List[str] = []
    for row in rows:
        fields += [key for key in row if key not in fields]
    with open(path, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


class Result(NamedTuple):
    label: str
    threads: int
    k: int
    ms: float
    lo: float
    hi: float
    gbs: float


def summarise(rows: Sequence[Dict], target_k: int) -> Dict[str, Result]:
    """step -> median and range over rounds, in the order the steps ran."""
    groups: Dict[str, List[Dict]] = {}
    for row in rows:
        if int(row["target_k"]) == target_k:
            groups.setdefault(row["step"], []).append(row)
    results = {}
    for step, members in groups.items():
        times = [float(r["median_ms"]) for r in members]
        mid = statistics.median(times)
        n = int(members[0]["n"])
        results[step] = Result(members[0]["label"], int(members[0]["threads"]), int(members[0]["k"]),
                               mid, min(times), max(times), n * 4 / mid / 1e6)
    return results


def table(header: Sequence[str], body: Sequence[Sequence]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(str(cell) for cell in row) + " |" for row in body]
    return "\n".join(lines) + "\n"


def ms(value: float) -> str:
    return f"{value:.3f}" if value < 100 else f"{value:.1f}"


def ratio(base: float, value: float) -> str:
    return f"{base / value:.2f}x"


def ladder_table(results: Dict[str, Result], ladder_steps: Sequence[str]) -> str:
    body = []
    previous = None
    heap, scalar = results.get("heap"), results.get("scalar")
    for step in ladder_steps:
        r = results[step]
        body.append([r.label, r.threads, ms(r.ms), f"{ms(r.lo)} to {ms(r.hi)}", f"{r.gbs:.1f}",
                     ratio(previous.ms, r.ms) if previous else "-",
                     ratio(heap.ms, r.ms) if heap else "-", ratio(scalar.ms, r.ms) if scalar else "-"])
        previous = r
    return table(["Step", "P", "Median ms", "Range ms", "GB/s", "vs previous", "vs min-heap", "vs scalar"],
                 body)


def filter_merge_table(results: Dict[str, Result], k: int) -> str:
    pairs = [("None", "no_filter_scalar_merge", "no_filter"),
             ("Scalar", "scalar_filter_scalar_merge", "scalar_filter"),
             ("Vector", "vector_filter_scalar_merge", "vector_filter")]
    body = [[name, ms(results[a].ms), ms(results[b].ms), ratio(results[a].ms, results[b].ms)]
            for name, a, b in pairs if a in results and b in results]
    return table(["Filter", f"Scalar merge (K = {k - 1}), ms", f"Vector merge (K = {k}), ms",
                  "Vector merge speedup"], body)


def write_report(out: Path) -> None:
    with open(out / "ablation.csv", newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise SystemExit("ablation.csv is empty")
    n = int(rows[0]["n"])
    rounds = len({r["round"] for r in rows})
    avx2 = rows[0]["avx2"] == "1"
    failed = sum(1 for r in rows if r["verified"] != "1")
    parts = ["# Optimisation ladder\n\n",
             f"N = {n}, uniform input. Each value is the median of {rounds} rounds, and each round is "
             "that configuration's median over its repetitions. "
             f"{len(rows)} rows, {failed} failed verification.\n\n"]
    if not avx2:
        parts.append("**This build has no AVX2.** Both merge columns use the portable path and the "
                     "vector filter is auto-vectorised, so report numbers need the x86 machine.\n\n")
    for k in sorted({int(r["target_k"]) for r in rows}):
        results = summarise(rows, k)
        ladder_steps = list(dict.fromkeys(r["step"] for r in rows
                                          if int(r["target_k"]) == k and r["ladder"] == "1"))
        parts += [f"## K = {k}\n\n", ladder_table(results, ladder_steps), "\n",
                  "Filter against merge, one thread:\n\n", filter_merge_table(results, k),
                  "\nThe scalar merge skips every value at or below the current bound by itself, so "
                  "the \"None\" row is still filtered value by value in its scalar column. Compare "
                  "the two merges only on the Scalar and Vector rows.\n"]
        if "no_tiling" in results and "vector_filter" in results:
            off, on = results["no_tiling"], results["vector_filter"]
            parts.append(f"\nTiling off (B = N): {ms(off.ms)} ms against {ms(on.ms)} ms at "
                         f"B = {DEFAULT_TILE}.\n")
        parts.append("\n")
    (out / "ladder.md").write_text("".join(parts))
    print(f"wrote {out / 'ladder.md'}")
    if failed:
        raise SystemExit(f"{failed} rows failed verification")


def newest_run() -> Path:
    runs = [p for p in (ROOT / "results").glob("*/") if not p.name.startswith("aggregate_")]
    if not runs:
        raise SystemExit("no run in results/: run ./scripts/run_all.sh first, or name a directory")
    return max(runs, key=lambda p: p.stat().st_mtime)


def record_system(run: Path, out: Path) -> None:
    info = harness.detect_system()
    info.update(harness.source_info())
    info.update(harness.power_info())
    (out / "system_info.json").write_text(json.dumps(info, indent=2))
    base = run / "system_info.json"
    if base.exists():
        known = json.loads(base.read_text()).get("source_sha256")
        if known and known != info["source_sha256"]:
            print(f"warning: the code changed since {run.name} was measured", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=Path, nargs="?", help="result directory; default: the newest")
    parser.add_argument("--rounds", type=int, default=3, help="interleaved repeats of the whole ladder")
    parser.add_argument("--reps", type=int, default=10, help="repetitions per configuration and round")
    parser.add_argument("--n", default="32M", help="input size")
    parser.add_argument("--k", default="64,1024", help="comma-separated; multiples of 8")
    parser.add_argument("--threads", default="2,4,8", help="OpenMP steps of the ladder")
    parser.add_argument("--report-only", action="store_true", help="rebuild from ablation.csv")
    parser.add_argument("--no-build", action="store_true", help="skip `make`")
    args = parser.parse_args()

    if any(int(k) % 8 for k in args.k.split(",")):
        raise SystemExit("--k needs multiples of 8: K - 1 is what takes the scalar merge path")
    run = args.run or newest_run()
    run.mkdir(parents=True, exist_ok=True)
    out = run / "ablation"
    if not args.report_only:
        if out.exists():
            raise SystemExit(f"{out} already exists: move it away first, it may be report data")
        if not args.no_build:
            subprocess.run(["make", "-C", str(ROOT), "-j", "all", "bench-ablation"], check=True)
        out.mkdir()
        record_system(run, out)
        measure(args, out, parse_size(args.n))
    write_report(out)

    plot = [sys.executable, str(Path(__file__).parent / "plot_results.py"), str(run),
            "--format", "png,pdf"]
    if subprocess.run(plot, capture_output=True).returncode != 0:
        print(f"figures not updated; run: {' '.join(plot)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
