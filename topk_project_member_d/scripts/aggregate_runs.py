#!/usr/bin/env python3
"""Repeated runs of one benchmark -> one result directory, plus how much they disagree.

    python3 scripts/aggregate_runs.py results/A_full results/B_full results/C_full
    python3 scripts/plot_results.py results/aggregate_<timestamp> --format png,pdf

Writes to --out (default results/aggregate_<timestamp>):
    throughput.csv, bandwidth.csv, perf_counters.csv
                       the same columns as one run, each value the median across runs, so
                       plot_results.py works on this directory unchanged; throughput.csv also
                       gets runs, median_ms_lo, median_ms_hi, spread_pct
    system_info.json   the first run's, plus aggregated_from
    variation.md       run metadata, run-to-run spread, best configurations, key speedups, and
                       the same configuration measured in different sweeps

spread_pct = (slowest run - fastest run) / median, on each run's median time. One directory is
accepted too: it can't show run-to-run spread, but the cross-sweep check still applies.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent

THROUGHPUT_KEY = ("sweep", "algo", "dist", "n", "k", "threads", "partitions", "tile", "merge",
                  "min_chunk")
BANDWIDTH_KEY = ("kernel", "threads", "bytes")
PERF_KEY = ("label", "algo", "n", "k", "threads", "merge", "tile", "reps", "roi")

# Identity of the experiment. A mismatch means the runs measure different things.
MUST_MATCH = ("cpu_model", "preset", "source_sha256", "git_commit")
# (a, b): "a vs b" is how many times faster a is than b.
COMPARISONS = (("tiled", "scalar"), ("tiled", "heap"), ("omp_tiled", "tiled"),
               ("omp_tiled", "omp_scalar"), ("omp_tiled", "omp_heap"), ("omp_tiled", "scalar"),
               ("omp_tiled", "heap"))


class Run:
    def __init__(self, path: Path):
        self.path = path
        self.name = path.name
        if not (path / "throughput.csv").exists():
            raise SystemExit(f"error: {path} has no throughput.csv")
        self.throughput = read_csv(path / "throughput.csv")
        self.bandwidth = read_csv(path / "bandwidth.csv")
        self.perf = read_csv(path / "perf_counters.csv")
        info_path = path / "system_info.json"
        self.info: Dict = json.loads(info_path.read_text()) if info_path.exists() else {}


def read_csv(path: Path) -> List[Dict[str, str]]:
    if not path.exists() or path.stat().st_size == 0:
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def number(text: Optional[str]) -> float:
    try:
        return float(text)  # type: ignore[arg-type]
    except (TypeError, ValueError):
        return math.nan


def is_numeric(text: Optional[str]) -> bool:
    """A number, or the literal "nan" that clean_number writes for a missing value."""
    return (text not in (None, "") and not math.isnan(number(text))) or text == "nan"


def clean_number(value: float) -> str:
    if math.isnan(value):
        return "nan"
    return str(int(value)) if value == int(value) else repr(round(value, 6))


def finite(values: Iterable[float]) -> List[float]:
    return [v for v in values if not math.isnan(v)]


def median_or_nan(values: Iterable[float]) -> float:
    kept = finite(values)
    return statistics.median(kept) if kept else math.nan


def percentile(values: Sequence[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def group_rows(tables: Sequence[List[Dict[str, str]]],
               key_cols: Sequence[str]) -> Dict[Tuple[str, ...], List[Dict[str, str]]]:
    """key -> one row per run that has it, in first-seen order."""
    groups: Dict[Tuple[str, ...], List[Dict[str, str]]] = {}
    for table in tables:
        for row in table:
            groups.setdefault(tuple(row.get(c, "") for c in key_cols), []).append(row)
    return groups


def median_row(rows: Sequence[Dict[str, str]], key_cols: Sequence[str]) -> Dict[str, str]:
    """Median of every numeric column; the key and any text columns come from the first row."""
    out = dict(rows[0])
    for col in rows[0]:
        if col not in key_cols and all(is_numeric(r.get(col)) for r in rows):
            out[col] = clean_number(median_or_nan(number(r[col]) for r in rows))
    return out


def combined_verified(rows: Sequence[Dict[str, str]]) -> str:
    """bench_topk writes 1 = matches the reference, 0 = wrong, -1 = not checked. One wrong run
    makes the configuration wrong; otherwise one unchecked run makes it unchecked."""
    states = {r.get("verified") for r in rows}
    return "0" if "0" in states else "-1" if "-1" in states else "1"


def aggregate_throughput(tables: Sequence[List[Dict[str, str]]]) -> List[Dict[str, str]]:
    out = []
    for rows in group_rows(tables, THROUGHPUT_KEY).values():
        row = median_row(rows, THROUGHPUT_KEY)
        times = finite(number(r.get("median_ms")) for r in rows)
        if times:
            mid, lo, hi = statistics.median(times), min(times), max(times)
            n = number(rows[0].get("n"))
            row["min_ms"] = clean_number(min(number(r.get("min_ms")) for r in rows))
            row["median_ms"] = clean_number(mid)
            # Rates come from the median time, so they agree with median_ms exactly
            row["melem_s"] = clean_number(n / mid / 1e3)
            row["gbs"] = clean_number(n * 4 / mid / 1e6)
            row["median_ms_lo"], row["median_ms_hi"] = clean_number(lo), clean_number(hi)
            row["spread_pct"] = clean_number(100 * (hi - lo) / mid)
        row["verified"] = combined_verified(rows)
        row["runs"] = str(len(rows))
        out.append(row)
    return out


def write_csv(path: Path, rows: Sequence[Dict[str, str]]) -> None:
    if not rows:
        return
    fields: List[str] = []
    for row in rows:
        fields += [k for k in row if k not in fields]
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def compatibility_problems(runs: Sequence[Run]) -> Tuple[List[str], List[str]]:
    errors, warnings = [], []
    for field in MUST_MATCH:
        values = {r.info.get(field) for r in runs}
        known = values - {None}
        if len(known) > 1:
            errors.append(f"{field} differs: {sorted(map(str, known))}")
        elif None in values:
            warnings.append(f"{field} is not recorded for every run, so it can't be compared")
    for run in runs:
        if run.info.get("git_dirty"):
            warnings.append(f"{run.name}: uncommitted changes in the tree")
        if run.info.get("on_mains_power") is False:
            warnings.append(f"{run.name}: ran on battery")
        failed = sum(1 for r in run.throughput if r.get("verified") == "0")
        if failed:
            warnings.append(f"{run.name}: {failed} configurations failed verification")
    governors = {r.info.get("cpu_governor") for r in runs}
    if len(governors) > 1:
        warnings.append(f"CPU governor differs: {sorted(map(str, governors))}")
    keys = [{tuple(r.get(c, "") for c in THROUGHPUT_KEY) for r in run.throughput} for run in runs]
    partial = len(set.union(*keys) - set.intersection(*keys))
    if partial:
        warnings.append(f"{partial} configurations are missing from at least one run")
    return errors, warnings


def md_table(header: Sequence[str], rows: Sequence[Sequence]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(str(c) for c in row) + " |" for row in rows]
    return "\n".join(lines) + "\n"


def describe(row: Dict[str, str]) -> str:
    parts = [row["algo"], f"N={row['n']}", f"K={row['k']}", f"P={row['threads']}"]
    if row.get("merge") not in (None, "", "none", "seq"):
        parts.append(row["merge"])
    if row.get("tile") not in (None, "", "0", "1024"):
        parts.append(f"B={row['tile']}")
    if row.get("dist") != "uniform":
        parts.append(row.get("dist", ""))
    return " ".join(parts)


def best_row(table: Sequence[Dict[str, str]], algo: str, n: str, k: str) -> Optional[Dict[str, str]]:
    """Fastest uniform-input row for an algorithm, over every P and merge strategy."""
    rows = [r for r in table if r.get("algo") == algo and r.get("n") == n and r.get("k") == k
            and r.get("dist") == "uniform" and r.get("tile") in ("0", "1024")
            and r.get("sweep") in ("size", "scaling")]
    return min(rows, key=lambda r: number(r["median_ms"])) if rows else None


def spread_section(merged: Sequence[Dict[str, str]]) -> str:
    rows = [r for r in merged if int(r["runs"]) > 1 and "spread_pct" in r]
    if not rows:
        return "\n## Run-to-run spread\n\nOnly one run, so there is no spread to report.\n"
    spreads = [number(r["spread_pct"]) for r in rows]
    parts = ["\n## Run-to-run spread\n\n",
             f"Across {len(rows)} configurations: median {statistics.median(spreads):.1f}%, "
             f"90th percentile {percentile(spreads, 0.9):.1f}%, maximum {max(spreads):.1f}%.\n",
             "\nNoisiest configurations:\n\n"]
    worst = sorted(rows, key=lambda r: -number(r["spread_pct"]))[:10]
    parts.append(md_table(["Sweep", "Configuration", "Median ms", "Range ms", "Spread"],
                          [[r["sweep"], describe(r), f"{number(r['median_ms']):.3f}",
                            f"{number(r['median_ms_lo']):.3f} to {number(r['median_ms_hi']):.3f}",
                            f"{number(r['spread_pct']):.1f}%"] for r in worst]))
    return "".join(parts)


def best_section(runs: Sequence[Run], n: str, ks: Sequence[str]) -> str:
    body = []
    for k in ks:
        for algo in ("omp_tiled", "tiled", "omp_scalar", "scalar", "omp_heap", "heap", "nth"):
            winners = [best_row(run.throughput, algo, n, k) for run in runs]
            winners = [w for w in winners if w]
            if not winners:
                continue
            times = [number(w["median_ms"]) for w in winners]
            configs = ", ".join(f"P={w['threads']}" + (f"/{w['merge']}" if w["merge"] != "none" else "")
                                for w in winners)
            body.append([k, algo, f"{statistics.median(times):.3f}",
                         f"{min(times):.3f} to {max(times):.3f}", configs])
    if not body:
        return ""
    return (f"\n## Best configuration at N = {n}\n\nEach run's fastest row per algorithm, over every "
            "P and merge strategy in the size and scaling sweeps.\n\n"
            + md_table(["K", "Algorithm", "Median ms", "Range ms", "Winner in each run"], body))


def comparison_section(runs: Sequence[Run], n: str, ks: Sequence[str]) -> str:
    body = []
    for k in ks:
        for fast, slow in COMPARISONS:
            ratios = []
            for run in runs:
                a, b = best_row(run.throughput, fast, n, k), best_row(run.throughput, slow, n, k)
                if a and b:
                    ratios.append(number(b["median_ms"]) / number(a["median_ms"]))
            if ratios:
                body.append([k, f"{fast} vs {slow}", f"{statistics.median(ratios):.2f}x",
                             f"{min(ratios):.2f}x to {max(ratios):.2f}x"])
    if not body:
        return ""
    return ("\n## Key speedups\n\nBest row against best row, computed inside each run, then the "
            "median and range. Each omp_ side uses its own fastest P, so the two sides can run "
            "different thread counts.\n\n" + md_table(["K", "Comparison", "Median", "Range"], body))


def cross_sweep_section(merged: Sequence[Dict[str, str]]) -> str:
    """The same configuration timed in two sweeps: the noise a single run can't average away."""
    key_cols = [c for c in THROUGHPUT_KEY if c != "sweep"]
    groups = group_rows([list(merged)], key_cols)
    body = []
    for rows in groups.values():
        if len(rows) < 2:
            continue
        times = [number(r["median_ms"]) for r in rows]
        lo, hi = min(times), max(times)
        detail = ", ".join(f"{r['sweep']} {number(r['median_ms']):.3f}" for r in rows)
        body.append((100 * (hi - lo) / lo, describe(rows[0]), detail))
    if not body:
        return ""
    body.sort(key=lambda row: -row[0])
    return (f"\n## Same configuration in different sweeps\n\n{len(body)} configurations appear in "
            "more than one sweep. Largest gaps:\n\n"
            + md_table(["Configuration", "Milliseconds", "Gap"],
                       [[name, detail, f"{gap:.1f}%"] for gap, name, detail in body[:10]]))


def run_table(runs: Sequence[Run]) -> str:
    body = []
    for run in runs:
        info = run.info
        commit = info.get("git_commit")
        digest = info.get("source_sha256")
        body.append([run.name, info.get("cpu_model", "?"), commit[:10] if commit else "-",
                     {True: "yes", False: "no"}.get(info.get("git_dirty"), "-"),
                     digest[:10] if digest else "-", info.get("cpu_governor") or "-",
                     {True: "yes", False: "no"}.get(info.get("on_mains_power"), "-"),
                     len(run.throughput)])
    return md_table(["Run", "CPU", "Commit", "Dirty", "Source hash", "Governor", "Mains", "Rows"], body)


def write_variation(path: Path, runs: Sequence[Run], merged: Sequence[Dict[str, str]],
                    warnings: Sequence[str]) -> None:
    parts = ["# Variation across runs\n\n", run_table(runs)]
    if warnings:
        parts += ["\n## Warnings\n\n"] + [f"- {w}\n" for w in warnings]
    parts.append(spread_section(merged))
    sizes = [r["n"] for r in merged if r.get("sweep") in ("size", "scaling")]
    if sizes:
        n_big = max(sizes, key=int)
        ks = sorted({r["k"] for r in merged if r.get("n") == n_big and r.get("sweep") in
                     ("size", "scaling")}, key=int)
        parts.append(best_section(runs, n_big, ks))
        parts.append(comparison_section(runs, n_big, ks))
    parts.append(cross_sweep_section(merged))
    path.write_text("".join(parts))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("runs", type=Path, nargs="+", help="result directories to combine")
    parser.add_argument("--out", type=Path, help="output directory")
    parser.add_argument("--allow-mixed", action="store_true",
                        help="combine runs even if the CPU, preset or code differs")
    args = parser.parse_args()

    runs = [Run(path) for path in args.runs]
    errors, warnings = compatibility_problems(runs)
    if errors and not args.allow_mixed:
        for message in errors:
            print(f"error: {message}", file=sys.stderr)
        print("these runs measure different things; --allow-mixed combines them anyway",
              file=sys.stderr)
        return 1
    warnings = [f"mixed runs: {m}" for m in errors] + warnings

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = args.out or ROOT / "results" / f"aggregate_{stamp}"
    out.mkdir(parents=True, exist_ok=True)

    merged = aggregate_throughput([r.throughput for r in runs])
    write_csv(out / "throughput.csv", merged)
    write_csv(out / "bandwidth.csv",
              [median_row(rows, BANDWIDTH_KEY)
               for rows in group_rows([r.bandwidth for r in runs], BANDWIDTH_KEY).values()])
    write_csv(out / "perf_counters.csv",
              [median_row(rows, PERF_KEY)
               for rows in group_rows([r.perf for r in runs], PERF_KEY).values()])
    info = dict(runs[0].info)
    info["aggregated_from"] = [r.name for r in runs]
    (out / "system_info.json").write_text(json.dumps(info, indent=2))
    write_variation(out / "variation.md", runs, merged, warnings)

    for message in warnings:
        print(f"warning: {message}", file=sys.stderr)
    print(f"{len(runs)} runs, {len(merged)} configurations -> {out}")
    print(f"next: python3 {Path(__file__).parent / 'plot_results.py'} {out} --format png,pdf")
    return 0


if __name__ == "__main__":
    sys.exit(main())
