#!/usr/bin/env python3
"""results/<timestamp>/ -> figures/*.{png,pdf} + summary.md. Only needs matplotlib.

    python3 scripts/plot_results.py results/<timestamp> [--format png,pdf]

Any input CSV may be missing; the figures that need it are skipped.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import matplotlib

if tuple(int(x) for x in matplotlib.__version__.split(".")[:2]) < (3, 3):  # set_xscale(base=)
    raise SystemExit(f"matplotlib >= 3.3 required, found {matplotlib.__version__}: "
                     "pip install -U matplotlib")
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter  # noqa: E402

# Fixed slot order: an algorithm keeps the same colour in every figure.
SURFACE, INK, INK2, MUTED, GRID, AXIS = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
SLOTS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
RAMP = ["#86b6ef", "#5598e7", "#2a78d6", "#1c5cab", "#104281", "#0d366b"]  # ordered series: K, threads

ALGO_COLOR = {"tiled": SLOTS[0], "omp_tiled": SLOTS[1], "heap": SLOTS[2],
              "nth": SLOTS[3], "omp_heap": SLOTS[4], "omp_nth": SLOTS[5]}
ALGO_LABEL = {"tiled": "Tiled SIMD engine (1 thread)", "omp_tiled": "OpenMP + tiled SIMD",
              "heap": "Min-heap baseline", "nth": "nth_element baseline",
              "omp_heap": "OpenMP + min-heap", "omp_nth": "OpenMP + nth_element"}
ALGO_ORDER = ["omp_tiled", "tiled", "omp_heap", "heap", "omp_nth", "nth"]
MERGE_COLOR = {"seq": SLOTS[0], "two": SLOTS[1], "tree": SLOTS[2]}
MERGE_LABEL = {"seq": "Sequential K-way", "two": "Two-level", "tree": "Pairwise tree"}
K_MARKERS = ["o", "s", "^", "D"]

STYLE = {
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": AXIS, "axes.labelcolor": INK2, "axes.titlecolor": INK,
    "axes.titlesize": 11, "axes.titleweight": "semibold", "axes.labelsize": 9.5,
    "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8, "grid.linestyle": "-",
    "xtick.color": MUTED, "ytick.color": MUTED, "xtick.labelsize": 8.5, "ytick.labelsize": 8.5,
    "xtick.labelcolor": INK2, "ytick.labelcolor": INK2,
    "lines.linewidth": 2.0, "lines.solid_capstyle": "round", "lines.solid_joinstyle": "round",
    "lines.markersize": 5.5, "lines.markeredgewidth": 1.2, "lines.markeredgecolor": SURFACE,
    "legend.frameon": False, "legend.fontsize": 8.5, "legend.labelcolor": INK2,
    "font.family": "sans-serif", "text.color": INK,
}
# labelcolor keys only exist from 3.4/3.5; older versions just lose those colours
plt.rcParams.update({k: v for k, v in STYLE.items() if k in plt.rcParams})


def load_csv(path: Path) -> List[Dict]:
    if not path.exists() or path.stat().st_size == 0:
        return []
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for row in rows:
        for key, value in list(row.items()):
            try:
                row[key] = float(value) if value not in ("", None) else math.nan
            except (TypeError, ValueError):
                pass
    return rows


def select(rows: Sequence[Dict], **conditions) -> List[Dict]:
    return [r for r in rows if all(r.get(k) == v for k, v in conditions.items())]


def unique(rows: Sequence[Dict], key: str) -> List:
    return sorted({r[key] for r in rows if key in r})


def fmt_count(x: float, _pos=None) -> str:
    for div, suffix in ((1 << 30, "G"), (1 << 20, "M"), (1 << 10, "K")):
        if x >= div and x % div == 0:
            return f"{int(x // div)}{suffix}"
    return f"{x:g}"


def fmt_bytes(x: float, _pos=None) -> str:
    for div, suffix in ((1 << 30, "GB"), (1 << 20, "MB"), (1 << 10, "KB")):
        if x >= div:
            return f"{x / div:g} {suffix}"
    return f"{x:g} B"


def fmt_rate(x: float, _pos=None) -> str:
    return f"{x / 1000:g}K" if x >= 1000 else f"{x:g}"


def log2_axis(ax, formatter=fmt_count) -> None:
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_formatter(FuncFormatter(formatter))
    ax.xaxis.set_minor_formatter(FuncFormatter(lambda *_: ""))


def caption(fig, text: str) -> None:
    fig.text(0.01, 0.005, text, fontsize=7.5, color=MUTED, ha="left", va="bottom")


def save(fig, out: Path, name: str, formats: Sequence[str]) -> None:
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    for ext in formats:
        fig.savefig(out / f"{name}.{ext}", dpi=170, bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)
    print(f"  wrote figures/{name}.[{','.join(formats)}]")


class Context:
    def __init__(self, results: Path):
        self.dir = results
        self.rows = load_csv(results / "throughput.csv")
        self.bw = load_csv(results / "bandwidth.csv")
        self.perf = load_csv(results / "perf_counters.csv")
        info_path = results / "system_info.json"
        self.info = json.loads(info_path.read_text()) if info_path.exists() else {}
        self.caches = {k: v for k, v in (self.info.get("cache_bytes") or {}).items() if v}

    def sweep(self, name: str) -> List[Dict]:
        return [r for r in self.rows if r.get("sweep") == name]

    def bw_threads(self) -> List[int]:
        return [int(p) for p in unique(self.bw, "threads")]

    def dram_gbs(self, threads: Optional[int] = None) -> Optional[float]:
        rows = self.bw if threads is None else select(self.bw, threads=float(threads))
        if not rows:
            return None
        biggest = max(r["bytes"] for r in rows)
        return max(r["gbs"] for r in rows if r["bytes"] == biggest)

    def cache_peak_gbs(self, threads: int, max_bytes: float) -> Optional[float]:
        rows = [r for r in select(self.bw, threads=float(threads)) if r["bytes"] <= max_bytes]
        return max((r["gbs"] for r in rows), default=None)

    def max_bw_threads(self) -> Optional[int]:
        ts = self.bw_threads()
        return max(ts) if ts else None


def median_by(rows: Sequence[Dict], key: str, value: str = "melem_s") -> Dict:
    groups = defaultdict(list)
    for r in rows:
        groups[r[key]].append(r[value])
    return {k: sorted(v)[len(v) // 2] for k, v in groups.items()}


def mark_caches(ax, ctx: Context, bytes_per_x: float = 4.0) -> None:
    for name, size in sorted(ctx.caches.items(), key=lambda kv: kv[1]):
        x = size / bytes_per_x
        lo, hi = ax.get_xlim()
        if lo < x < hi:
            ax.axvline(x, color=AXIS, linewidth=1, zorder=0)
            ax.text(x, 0.01, f" {name}", transform=ax.get_xaxis_transform(), fontsize=7.5,
                    color=MUTED, va="bottom", ha="left")


def ceiling(ax, y: float, label: str) -> None:
    lo, hi = ax.get_ylim()
    if y * 1.02 >= hi:
        ax.set_ylim(lo, y * (1.3 if ax.get_yscale() == "log" else 1.1))
    ax.axhline(y, color=MUTED, linewidth=1.2, zorder=1)
    ax.text(1.0, y, f"{label} ", transform=ax.get_yaxis_transform(), fontsize=7.5,
            color=INK2, ha="right", va="bottom")


def fig_throughput_vs_n(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.sweep("size")
    if not rows:
        return
    ks = unique(rows, "k")
    fig, axes = plt.subplots(1, len(ks), figsize=(4.6 * len(ks) + 2.6, 4.2), squeeze=False)
    for ax, k in zip(axes[0], ks):
        krows = select(rows, k=k)
        for algo in [a for a in ALGO_ORDER if select(krows, algo=a)]:
            series = median_by(select(krows, algo=algo), "n")
            xs = sorted(series)
            ax.plot(xs, [series[x] for x in xs], marker="o", color=ALGO_COLOR[algo],
                    label=ALGO_LABEL[algo])
        log2_axis(ax)
        ax.set_yscale("log")
        ax.yaxis.set_major_formatter(FuncFormatter(fmt_rate))
        mark_caches(ax, ctx)
        for p in ctx.bw_threads():
            gbs = ctx.dram_gbs(p)
            if gbs:
                ceiling(ax, gbs * 1e3 / 4, f"DRAM stream ceiling, {p} thr")
        ax.set_title(f"K = {int(k)}")
        ax.set_xlabel("N (elements)")
        ax.set_ylabel("Throughput (MElem/s)")
    axes[0][-1].legend(loc="upper left", bbox_to_anchor=(1.02, 1.0))
    caption(fig, "Median of repeated runs, uniform input. Vertical lines: N x 4 B equals cache capacity.")
    save(fig, out, "throughput_vs_n", fmts)


def fig_throughput_vs_k(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.sweep("k")
    if not rows:
        return
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in [a for a in ALGO_ORDER if select(rows, algo=a)]:
        series = median_by(select(rows, algo=algo), "k")
        xs = sorted(series)
        ax.plot(xs, [series[x] for x in xs], marker="o", color=ALGO_COLOR[algo], label=ALGO_LABEL[algo])
    log2_axis(ax)
    ax.set_yscale("log")
    ax.yaxis.set_major_formatter(FuncFormatter(fmt_rate))
    n = int(rows[0]["n"])
    ax.set_title(f"Throughput vs K (N = {fmt_count(n)})")
    ax.set_xlabel("K")
    ax.set_ylabel("Throughput (MElem/s)")
    ax.legend()
    save(fig, out, "throughput_vs_k", fmts)


def fig_speedup(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.sweep("size")
    ks = unique(rows, "k")
    if not rows:
        return
    k = 64.0 if 64.0 in ks else ks[0]
    krows = select(rows, k=k)
    baselines = [b for b in ("heap", "nth") if select(krows, algo=b)]
    if not baselines:
        return
    fig, axes = plt.subplots(1, len(baselines), figsize=(4.6 * len(baselines), 4.0), squeeze=False)
    for ax, base in zip(axes[0], baselines):
        base_t = median_by(select(krows, algo=base), "n", "median_ms")
        for algo in ("omp_tiled", "tiled", "omp_heap"):
            t = median_by(select(krows, algo=algo), "n", "median_ms")
            xs = sorted(set(t) & set(base_t))
            if xs:
                ax.plot(xs, [base_t[x] / t[x] for x in xs], marker="o", color=ALGO_COLOR[algo],
                        label=ALGO_LABEL[algo])
        ax.axhline(1.0, color=MUTED, linewidth=1.2)
        log2_axis(ax)
        ax.set_yscale("log")
        ax.yaxis.set_major_locator(LogLocator(base=10, subs=(1, 2, 5)))
        ax.yaxis.set_major_formatter(FuncFormatter(lambda y, _p: f"{y:g}x"))
        ax.yaxis.set_minor_formatter(NullFormatter())
        ax.set_title(f"Speedup over {ALGO_LABEL[base].lower()} (K = {int(k)})")
        ax.set_xlabel("N (elements)")
        ax.set_ylabel("Speedup")
    axes[0][0].legend()
    save(fig, out, "speedup_vs_baseline", fmts)


def _scaling_series(rows: Sequence[Dict]):
    """(algo, k) -> {threads: median_ms}; sequential merge only."""
    series = {}
    for algo in unique(rows, "algo"):
        for k in unique(select(rows, algo=algo), "k"):
            sel = [r for r in select(rows, algo=algo, k=k) if r.get("merge") in ("seq", None)]
            if sel:
                series[(algo, k)] = median_by(sel, "threads", "median_ms")
    return series


def fig_scaling(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.sweep("scaling")
    if not rows:
        return
    n = rows[0]["n"]
    series = _scaling_series(rows)
    ks = sorted({k for _, k in series})
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10.5, 4.2))
    max_p = max(unique(rows, "threads"))
    ax1.plot([1, max_p], [1, max_p], color=MUTED, linewidth=1.2)
    ax1.text(max_p, max_p, "ideal ", fontsize=7.5, color=INK2, ha="right", va="bottom")
    for (algo, k), t in sorted(series.items(), key=lambda kv: (ALGO_ORDER.index(kv[0][0]), kv[0][1])):
        ps = sorted(t)
        if 1.0 not in t:
            continue
        style = dict(color=ALGO_COLOR[algo], marker=K_MARKERS[ks.index(k) % 4],
                     linestyle="-" if ks.index(k) == 0 else (0, (4, 2)),
                     label=f"{ALGO_LABEL[algo]}, K = {int(k)}")
        ax1.plot(ps, [t[1.0] / t[p] for p in ps], **style)
        ax2.plot(ps, [n * 4 / (t[p] * 1e-3) / 1e9 for p in ps], **style)
    for ax in (ax1, ax2):
        ax.set_xlabel("Threads P")
        ax.set_xticks(unique(rows, "threads"))
        ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _p: f"{int(x)}"))
    ax1.set_ylabel("Speedup vs P = 1")
    ax1.set_title(f"Strong scaling (N = {fmt_count(n)})")
    ax2.set_ylabel("Effective bandwidth (GB/s)")
    ax2.set_title("Input bandwidth vs threads")
    ax2.set_ylim(bottom=0)
    dram = ctx.dram_gbs(ctx.max_bw_threads()) if ctx.bw else None
    if dram:
        ceiling(ax2, dram, f"DRAM stream ceiling ({ctx.max_bw_threads()} thr)")
    peak = ctx.info.get("dram_peak_gbs")
    if peak:
        ceiling(ax2, peak, "theoretical DRAM peak")
    ax1.legend(loc="upper left")
    caption(fig, "Sequential merge. Effective bandwidth = N x 4 B / median time.")
    save(fig, out, "thread_scaling", fmts)


def fig_merge(ctx: Context, out: Path, fmts) -> None:
    rows = select(ctx.sweep("scaling"), algo="omp_tiled")
    merges = [m for m in ("seq", "two", "tree") if select(rows, merge=m)]
    if len(merges) < 2:
        return
    ks = unique(rows, "k")
    fig, axes = plt.subplots(1, len(ks), figsize=(4.6 * len(ks), 4.0), squeeze=False)
    for ax, k in zip(axes[0], ks):
        for m in merges:
            t = median_by(select(rows, k=k, merge=m), "threads", "merge_ms")
            ps = [p for p in sorted(t) if p > 1]
            ax.plot(ps, [t[p] * 1e3 for p in ps], marker="o", color=MERGE_COLOR[m], label=MERGE_LABEL[m])
        ax.set_xticks([p for p in unique(rows, "threads") if p > 1])
        ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _p: f"{int(x)}"))
        ax.set_ylim(bottom=0)
        ax.set_title(f"Merge phase cost (K = {int(k)})")
        ax.set_xlabel("Threads P")
        ax.set_ylabel("Merge time (µs)")
    axes[0][0].legend()
    caption(fig, "Time from the end of the local phase to the final Top-K (includes barriers / join).")
    save(fig, out, "merge_overhead", fmts)


def fig_weak(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.sweep("weak")
    if not rows:
        return
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    t = {r["threads"]: r for r in rows}
    ps = sorted(t)
    ax.plot(ps, [t[p]["gbs"] for p in ps], marker="o", color=ALGO_COLOR["omp_tiled"],
            label=ALGO_LABEL["omp_tiled"])
    dram = ctx.dram_gbs(ctx.max_bw_threads()) if ctx.bw else None
    if dram:
        ceiling(ax, dram, "DRAM stream ceiling")
    ax.set_xticks(ps)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _p: f"{int(x)}"))
    ax.set_ylim(bottom=0)
    per = rows[0]["n"] / rows[0]["threads"]
    ax.set_title(f"Weak scaling ({fmt_count(per)} elements per thread, K = {int(rows[0]['k'])})")
    ax.set_xlabel("Threads P")
    ax.set_ylabel("Effective bandwidth (GB/s)")
    save(fig, out, "weak_scaling", fmts)


def fig_tile(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.sweep("tile")
    if not rows:
        return
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    ks = unique(rows, "k")
    ax.axvspan(512, 4096, color=GRID, alpha=0.5, zorder=0, linewidth=0)
    for i, k in enumerate(ks):
        t = median_by(select(rows, k=k), "tile")
        xs = sorted(t)
        color = RAMP[min(len(RAMP) - 1, 1 + i * max(1, (len(RAMP) - 1) // max(1, len(ks))))]
        ax.plot(xs, [t[x] for x in xs], marker="o", color=color, label=f"K = {int(k)}")
        best = max(xs, key=lambda x: t[x])
        ax.annotate(f"{fmt_count(best)}", (best, t[best]), textcoords="offset points",
                    xytext=(0, 7), ha="center", fontsize=7.5, color=INK2)
    log2_axis(ax)
    mark_caches(ax, ctx)
    lo, hi = ax.get_ylim()
    ax.set_ylim(lo, hi + 0.12 * (hi - lo))
    ax.set_title(f"Tile size sweep, single-thread tiled engine (N = {fmt_count(rows[0]['n'])})")
    ax.set_xlabel("Tile size B (floats)")
    ax.set_ylabel("Throughput (MElem/s)")
    ax.legend(title="", loc="lower right")
    caption(fig, "Shaded: B in [512, 4096] (project spec). Vertical lines: B x 4 B equals cache capacity.")
    save(fig, out, "tile_sweep", fmts)


def fig_distributions(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.sweep("dist")
    if not rows:
        return
    dists = [d for d in ("uniform", "gaussian", "ascending", "descending", "duplicates", "special")
             if select(rows, dist=d)]
    algos = [a for a in ALGO_ORDER if select(rows, algo=a)]
    # Dot plot on a log axis: ascending input is >100x slower and would vanish as a bar.
    fig, ax = plt.subplots(figsize=(8.0, 0.55 * len(dists) + 1.6))
    for j, d in enumerate(dists):
        vals = [median_by(select(rows, algo=a, dist=d), "dist").get(d) for a in algos]
        present = [v for v in vals if v]
        if present:
            ax.plot([min(present), max(present)], [j, j], color=GRID, linewidth=2, zorder=1)
    for algo in algos:
        pts = [(median_by(select(rows, algo=algo, dist=d), "dist").get(d), j) for j, d in enumerate(dists)]
        pts = [(v, j) for v, j in pts if v]
        ax.plot([v for v, _ in pts], [j for _, j in pts], marker="o", markersize=8, linestyle="none",
                color=ALGO_COLOR[algo], label=ALGO_LABEL[algo], zorder=3)
    ax.set_yticks(range(len(dists)))
    ax.set_yticklabels(dists)
    ax.invert_yaxis()
    ax.grid(axis="y", visible=False)
    ax.set_xscale("log")
    ax.xaxis.set_major_formatter(FuncFormatter(fmt_rate))
    ax.set_xlabel("Throughput (MElem/s, log scale)")
    ax.set_title(f"Sensitivity to input distribution (N = {fmt_count(rows[0]['n'])}, K = {int(rows[0]['k'])})")
    ax.legend(loc="lower left", fontsize=8)
    caption(fig, "ascending = worst case for threshold filtering (every block survives); "
                 "special = 1% each of +inf, -inf, NaN.")
    save(fig, out, "distributions", fmts)


def fig_bandwidth(ctx: Context, out: Path, fmts) -> None:
    if not ctx.bw:
        return
    fig, ax = plt.subplots(figsize=(6.8, 4.2))
    threads = ctx.bw_threads()
    for i, p in enumerate(threads):
        rows = sorted(select(ctx.bw, threads=float(p)), key=lambda r: r["bytes"])
        color = RAMP[min(len(RAMP) - 1, 2 + 2 * i)]
        ax.plot([r["bytes"] for r in rows], [r["gbs"] for r in rows], marker="o", color=color,
                label=f"{p} thread{'s' if p > 1 else ''}")
    log2_axis(ax, fmt_bytes)
    mark_caches(ax, ctx, bytes_per_x=1.0)
    peak = ctx.info.get("dram_peak_gbs")
    if peak:
        ceiling(ax, peak, "theoretical DRAM peak")
    ax.set_ylim(bottom=0)
    ax.set_title("Memory hierarchy: streaming max (Top-1 speed of light)")
    ax.set_xlabel("Working set")
    ax.set_ylabel("Read bandwidth (GB/s)")
    ax.legend()
    save(fig, out, "bandwidth", fmts)


def fig_roofline(ctx: Context, out: Path, fmts) -> None:
    size_rows = ctx.sweep("size")
    if not size_rows or not ctx.bw:
        return
    p_max = ctx.max_bw_threads()
    dram = ctx.dram_gbs(p_max)
    l2 = ctx.caches.get("L2", 1 << 20)
    cache_peak = ctx.cache_peak_gbs(p_max, l2)
    # Compute roof only from a user-given peak; deriving one from a memory probe would be fiction.
    compute = ctx.info.get("peak_gops")
    oi_dram = 0.25  # one compare per 4-byte element
    fig, ax = plt.subplots(figsize=(6.8, 4.6))
    lo_x, hi_x = 2 ** -5, 2 ** 3
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    roofs = [("DRAM", dram, MUTED), ("Cache (<= L2)", cache_peak, AXIS),
             ("Theoretical DRAM peak", ctx.info.get("dram_peak_gbs"), GRID)]
    for idx, (name, bw, color) in enumerate(roofs):
        if not bw:
            continue
        ridge = compute / bw if compute else hi_x
        xs = [lo_x, min(ridge, hi_x)] + ([hi_x] if compute and ridge < hi_x else [])
        ys = [bw * x if x <= ridge else compute for x in xs]
        ax.plot(xs, ys, color=color, linewidth=1.4, zorder=1)
        label_x = lo_x * 1.3 * 2.5 ** idx  # staggered, else adjacent roof labels overlap
        ax.text(label_x, bw * label_x * 1.2, f"{name} {bw:.0f} GB/s", fontsize=7.5, color=INK2)
    if compute:
        ax.text(hi_x, compute * 1.08, f"compute peak {compute:g} G/s ", fontsize=7.5, color=INK2, ha="right")
    ax.axvline(oi_dram, color=GRID, linewidth=1, zorder=0)
    n_big = max(unique(size_rows, "n"))
    big = [r for r in size_rows if r["n"] == n_big]
    small_n = min(unique(size_rows, "n"))
    small = [r for r in size_rows if r["n"] == small_n]
    for algo in [a for a in ALGO_ORDER if select(big, algo=a)]:
        best = max(select(big, algo=algo), key=lambda r: r["melem_s"])
        ax.plot([oi_dram], [best["melem_s"] / 1e3], marker="o", markersize=8, color=ALGO_COLOR[algo],
                linestyle="none", label=f"{ALGO_LABEL[algo]} (N = {fmt_count(n_big)})")
        cached = select(small, algo=algo)
        if cached:
            c = max(cached, key=lambda r: r["melem_s"])
            ax.plot([oi_dram * 1.25], [c["melem_s"] / 1e3], marker="o", markersize=7, linestyle="none",
                    markerfacecolor=SURFACE, markeredgecolor=ALGO_COLOR[algo], markeredgewidth=1.8)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _p: f"{x:g}"))
    ax.set_xlabel("Operational intensity (compares / byte)")
    ax.set_ylabel("Performance (G compares/s)")
    ax.set_title("Roofline: Top-K implementations vs memory roofs")
    ax.legend(loc="lower right", fontsize=7.5)
    caption(fig, f"OI = 1 compare / 4-byte element. Filled: N = {fmt_count(n_big)}; hollow: N = "
                 f"{fmt_count(small_n)} (offset for legibility). Roofs at max threads.")
    save(fig, out, "roofline", fmts)


COUNTER_PANELS = [
    ("instructions_per_elem", "Instructions / element"),
    ("branch-misses_per_elem", "Branch misses / element"),
    ("L1-dcache-load-misses_per_elem", "L1d load misses / element"),
    ("LLC-load-misses_per_elem", "LLC load misses / element"),
    ("ipc", "IPC (instructions / cycle)"),
]


def fig_counters(ctx: Context, out: Path, fmts) -> None:
    rows = ctx.perf
    if not rows:
        return
    for r in rows:
        cyc, ins = r.get("cycles", math.nan), r.get("instructions", math.nan)
        r["ipc"] = ins / cyc if cyc and cyc == cyc and cyc > 0 else math.nan
    ks = unique(rows, "k")
    fig, axes = plt.subplots(len(ks), len(COUNTER_PANELS), figsize=(3.0 * len(COUNTER_PANELS), 3.3 * len(ks)),
                             squeeze=False)
    for row_axes, k in zip(axes, ks):
        krows = select(rows, k=k)
        labels = [f"{r['algo']}\nP={int(r['threads'])}" for r in krows]
        for ax, (key, title) in zip(row_axes, COUNTER_PANELS):
            vals = [r.get(key, math.nan) for r in krows]
            ax.bar(range(len(krows)), [v if v == v else 0 for v in vals], width=0.6,
                   color=[ALGO_COLOR.get(r["algo"], SLOTS[0]) for r in krows], edgecolor=SURFACE,
                   linewidth=2, zorder=2)
            ax.set_xticks(range(len(krows)))
            ax.set_xticklabels(labels, fontsize=7)
            ax.grid(axis="x", visible=False)
            ax.set_title(f"{title} (K = {int(k)})", fontsize=9)
            for i, v in enumerate(vals):
                if v != v:
                    ax.text(i, 0, "n/a", ha="center", va="bottom", fontsize=7, color=MUTED)
    caption(fig, "perf stat counters restricted to the timed repetitions, normalised per input element.")
    save(fig, out, "counters", fmts)


def md_table(header: Sequence[str], rows: Sequence[Sequence]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(str(c) for c in row) + " |" for row in rows]
    return "\n".join(lines) + "\n"


def write_summary(ctx: Context, out_md: Path) -> None:
    info = ctx.info
    parts = ["# Top-K benchmark summary\n"]
    parts.append(md_table(["Field", "Value"], [
        ["CPU", info.get("cpu_model", "?")],
        ["Physical / logical cores", f"{info.get('physical_cores')} / {info.get('logical_cpus')}"],
        ["Caches", ", ".join(f"{k} {fmt_bytes(v)}" for k, v in ctx.caches.items()) or "?"],
        ["perf", info.get("perf")],
        ["Preset", info.get("preset")],
        ["Timestamp", info.get("timestamp")],
    ]))

    verified = [r.get("verified") for r in ctx.rows]
    parts.append(f"\nVerified configurations: {sum(1 for v in verified if v == 1)} passed, "
                 f"{sum(1 for v in verified if v == 0)} failed, "
                 f"{sum(1 for v in verified if v not in (0, 1))} not checked.\n")

    size = ctx.sweep("size")
    if size:
        n_big = max(unique(size, "n"))
        parts.append(f"\n## Largest input (N = {fmt_count(n_big)})\n\n")
        body = []
        for k in unique(size, "k"):
            rows = select(size, n=n_big, k=k)
            heap = select(rows, algo="heap")
            nth = select(rows, algo="nth")
            for r in sorted(rows, key=lambda r: ALGO_ORDER.index(r["algo"])):
                p = int(r["threads"])
                bw = ctx.dram_gbs(p) if p in ctx.bw_threads() else None
                body.append([
                    ALGO_LABEL.get(r["algo"], r["algo"]), int(k), p, f"{r['median_ms']:.3f}",
                    f"{r['melem_s']:.0f}", f"{r['gbs']:.1f}",
                    f"{100 * r['gbs'] / bw:.0f}%" if bw else "-",
                    f"{heap[0]['median_ms'] / r['median_ms']:.2f}x" if heap else "-",
                    f"{nth[0]['median_ms'] / r['median_ms']:.2f}x" if nth else "-",
                ])
        parts.append(md_table(["Implementation", "K", "P", "Median ms", "MElem/s", "GB/s",
                               "% of DRAM stream (same P)", "vs heap", "vs nth"], body))

    scaling = ctx.sweep("scaling")
    if scaling:
        parts.append(f"\n## Strong scaling (N = {fmt_count(scaling[0]['n'])}, sequential merge)\n\n")
        body = []
        for (algo, k), t in sorted(_scaling_series(scaling).items()):
            if 1.0 not in t:
                continue
            for p in sorted(t):
                s = t[1.0] / t[p]
                body.append([ALGO_LABEL.get(algo, algo), int(k), int(p), f"{t[p]:.3f}",
                             f"{s:.2f}x", f"{100 * s / p:.0f}%"])
        parts.append(md_table(["Implementation", "K", "P", "Median ms", "Speedup", "Efficiency"], body))

        omp = select(scaling, algo="omp_tiled")
        merges = [m for m in ("seq", "two", "tree") if select(omp, merge=m)]
        if len(merges) > 1:
            parts.append("\n## Merge-phase cost (µs, omp_tiled)\n\n")
            body = []
            for k in unique(omp, "k"):
                for p in [p for p in unique(omp, "threads") if p > 1]:
                    cells = []
                    for m in merges:
                        sel = select(omp, k=k, threads=p, merge=m)
                        cells.append(f"{sel[0]['merge_ms'] * 1e3:.1f}" if sel else "-")
                    body.append([int(k), int(p), *cells])
            parts.append(md_table(["K", "P", *[MERGE_LABEL[m] for m in merges]], body))

    tile = ctx.sweep("tile")
    if tile:
        parts.append("\n## Best tile size\n\n")
        body = []
        for k in unique(tile, "k"):
            t = median_by(select(tile, k=k), "tile")
            best = max(t, key=t.get)
            body.append([int(k), int(best), f"{t[best]:.0f}",
                         " / ".join(f"{int(b)}: {v:.0f}" for b, v in sorted(t.items()))])
        parts.append(md_table(["K", "Best B", "MElem/s", "All B: MElem/s"], body))

    dist = ctx.sweep("dist")
    if dist:
        parts.append("\n## Input distributions (MElem/s)\n\n")
        algos = [a for a in ALGO_ORDER if select(dist, algo=a)]
        body = []
        for d in unique(dist, "dist"):
            body.append([d, *[f"{select(dist, dist=d, algo=a)[0]['melem_s']:.0f}"
                              if select(dist, dist=d, algo=a) else "-" for a in algos]])
        parts.append(md_table(["Distribution", *[ALGO_LABEL[a] for a in algos]], body))

    if ctx.bw:
        parts.append("\n## Bandwidth ceilings (stream max)\n\n")
        body = []
        l2 = ctx.caches.get("L2", 1 << 20)
        for p in ctx.bw_threads():
            body.append([p, f"{ctx.cache_peak_gbs(p, l2) or 0:.1f}", f"{ctx.dram_gbs(p) or 0:.1f}"])
        parts.append(md_table(["Threads", "Peak in-cache GB/s (<= L2)", "DRAM GB/s (largest size)"], body))
        if info.get("dram_peak_gbs"):
            parts.append(f"\nTheoretical DRAM peak: {info['dram_peak_gbs']} GB/s\n")

    if ctx.perf:
        parts.append("\n## Hardware counters (per element, timed region only)\n\n")
        body = []
        for r in ctx.perf:
            cyc, ins = r.get("cycles", math.nan), r.get("instructions", math.nan)
            body.append([r["algo"], int(r["threads"]), int(r["k"]), r.get("roi"),
                         *[f"{r.get(e + '_per_elem', math.nan):.4f}" for e in
                           ("instructions", "branch-misses", "L1-dcache-load-misses", "LLC-load-misses")],
                         f"{ins / cyc:.2f}" if cyc and cyc == cyc else "-"])
        parts.append(md_table(["Algo", "P", "K", "ROI", "instr", "branch-miss", "L1d miss",
                               "LLC miss", "IPC"], body))

    out_md.write_text("".join(parts))
    print(f"  wrote {out_md.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot Member D benchmark results")
    parser.add_argument("results", type=Path, help="results directory from run_benchmarks.py")
    parser.add_argument("--format", default="png", help="comma-separated: png,pdf,svg")
    parser.add_argument("--dram-peak-gbs", type=float, help="override theoretical DRAM peak")
    parser.add_argument("--peak-gops", type=float,
                        help="theoretical compare throughput (G/s) for the roofline compute roof")
    args = parser.parse_args()

    ctx = Context(args.results)
    if args.dram_peak_gbs:
        ctx.info["dram_peak_gbs"] = args.dram_peak_gbs
    if args.peak_gops:
        ctx.info["peak_gops"] = args.peak_gops
    if not ctx.rows and not ctx.bw and not ctx.perf:
        print(f"no CSV data in {args.results}")
        return 1
    out = args.results / "figures"
    out.mkdir(exist_ok=True)
    fmts = [f.strip() for f in args.format.split(",") if f.strip()]
    for fig in (fig_throughput_vs_n, fig_throughput_vs_k, fig_speedup, fig_scaling, fig_merge,
                fig_weak, fig_tile, fig_distributions, fig_bandwidth, fig_roofline, fig_counters):
        try:
            fig(ctx, out, fmts)
        except Exception as exc:  # one broken panel shouldn't cost the rest
            print(f"  ! {fig.__name__} failed: {exc!r}")
    write_summary(ctx, args.results / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
