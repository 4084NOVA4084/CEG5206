#!/usr/bin/env bash
# Deep dive on ONE config: ./scripts/perf_profile.sh --n 32M --algo omp_tiled --k 1024 --threads 8
# annotate.txt is the annotated assembly of the hottest symbol: use it to check that the filter
# and merge loops have no data-dependent branches.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="$HERE/build/bench_topk"
[[ -x "$BENCH" ]] || make -C "$HERE" all
command -v perf >/dev/null || { echo "perf not found (Linux only)"; exit 1; }

ARGS=("$@")
[[ ${#ARGS[@]} -eq 0 ]] && ARGS=(--n 32M --algo omp_tiled --k 64 --threads 0)
OUT="$HERE/profile_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
export OMP_PROC_BIND="${OMP_PROC_BIND:-close}" OMP_PLACES="${OMP_PLACES:-cores}"

# counters over the timed reps only
mkfifo "$OUT/ctl" "$OUT/ack"
if ! perf stat -d -d --delay=-1 --control="fifo:$OUT/ctl,$OUT/ack" -o "$OUT/stat.txt" -- \
        "$BENCH" "${ARGS[@]}" --reps 20 --perf-ctl "$OUT/ctl" --perf-ack "$OUT/ack" --csv-header; then
    echo "perf --control unsupported; counting the whole process instead"
    perf stat -d -d -o "$OUT/stat.txt" -- "$BENCH" "${ARGS[@]}" --reps 20 --csv-header
fi
rm -f "$OUT/ctl" "$OUT/ack"

# -g call graphs rely on the Makefile keeping frame pointers
perf record -g -o "$OUT/perf.data" -- "$BENCH" "${ARGS[@]}" --reps 50 >/dev/null
perf report -i "$OUT/perf.data" --stdio --no-children --percent-limit 1 > "$OUT/report.txt" 2>/dev/null

HOT="$(perf report -i "$OUT/perf.data" --stdio --no-children -F sym 2>/dev/null \
        | grep -m1 -E '^\s+[0-9.]+%' | sed -E 's/.*\] //' || true)"
if [[ -n "$HOT" ]]; then
    perf annotate -i "$OUT/perf.data" --stdio "$HOT" > "$OUT/annotate.txt" 2>/dev/null || true
fi

echo "== $OUT"
cat "$OUT/stat.txt"
echo "hottest symbol: ${HOT:-?}  (see report.txt / annotate.txt)"
