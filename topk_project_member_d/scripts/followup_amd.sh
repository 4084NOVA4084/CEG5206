#!/usr/bin/env bash
# The measurements behind report sections 8 and 9 that the main sweep doesn't take.
# Zen 3 doesn't support LLC-load-misses, so its L2 prefetch events stand in for it.
#
#   ./scripts/followup_amd.sh [results/<run>]      default: the newest run
#
# Writes <run>/followup/:
#   bandwidth_all_threads.csv      pure stream read at every thread count
#   merge_path_k1024_vs_k1023.csv  AVX2 merge (K = 1024) against the scalar path (K = 1023)
#   l2_prefetch_events.csv         L3 hits and memory fetches, tiled engine, small and large N
#   l3_by_algorithm.csv            memory fetches per cache line for each algorithm
#   README.txt, commands.log, bench.stderr
#
# The last two CSVs need Linux, perf with --control and an AMD Zen CPU. Without those the
# first two are still written and the script exits non-zero.
# Smaller inputs for a smoke test: N_SMALL=256K N_BIG=1M BW_SIZE=16M BW_THREADS=1,2
set -euo pipefail

RUN="${1:-}"
if [[ -n "$RUN" ]]; then
    [[ -d "$RUN" ]] || { echo "no such directory: $RUN"; exit 1; }
    RUN="$(cd "$RUN" && pwd)"
fi
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ -z "$RUN" ]]; then
    # aggregate_* directories hold medians, not a run
    RUN="$(ls -td results/*/ 2>/dev/null | grep -v '/aggregate_' | head -1 || true)"
    [[ -n "$RUN" ]] || { echo "no run in results/: run ./scripts/run_all.sh first"; exit 1; }
    RUN="${RUN%/}"
fi
OUT="$RUN/followup"
if [[ -e "$OUT" ]]; then
    echo "$OUT already exists: move it away first, because it may hold report data"
    exit 1
fi

N_SMALL="${N_SMALL:-1M}"
N_BIG="${N_BIG:-32M}"
BW_SIZE="${BW_SIZE:-512M}"
BW_THREADS="${BW_THREADS:-1,2,3,4,6,8,12,16}"
EVENTS_L2="l2_pf_miss_l2_hit_l3,l2_pf_miss_l2_l3"
EVENTS_L3="$EVENTS_L2,ls_dmnd_fills_from_sys.mem_io_local"
BENCH=./build/bench_topk
BANDWIDTH=./build/bench_bandwidth

export OMP_PROC_BIND="${OMP_PROC_BIND:-close}"
export OMP_PLACES="${OMP_PLACES:-cores}"

make -j all
mkdir -p "$OUT"

# Runs a command, logging it; stdout goes to the file named first.
run_to() {
    local target="$1"
    shift
    printf '%s\n' "$*" >> "$OUT/commands.log"
    "$@" > "$target" 2>> "$OUT/bench.stderr" || {
        tail -3 "$OUT/bench.stderr"
        echo "failed: $*"
        exit 1
    }
}

# Sets SKIP_REASON and fails if perf can't count these events inside a control window.
perf_ready() {
    command -v perf > /dev/null || { SKIP_REASON="perf not found"; return 1; }
    local tmp
    tmp="$(mktemp -d)"
    mkfifo "$tmp/ctl" "$tmp/ack"
    local status=0
    perf stat -x, -e "$EVENTS_L3" --delay=-1 "--control=fifo:$tmp/ctl,$tmp/ack" -- true \
        > /dev/null 2> "$tmp/err" || status=$?
    if [[ "$status" -ne 0 ]] || grep -q 'not supported' "$tmp/err"; then
        SKIP_REASON="perf can't count $EVENTS_L3 with --control: $(tail -1 "$tmp/err")"
        rm -rf "$tmp"
        return 1
    fi
    rm -rf "$tmp"
}

# perf_row EVENTS ALGO K N REPS prints algo,k,threads,n,reps,median_ms,<one value per event>.
# bench_topk opens the counters around the timed reps only, as run_benchmarks.py does.
perf_row() {
    local events="$1" algo="$2" k="$3" n="$4" reps="$5" tmp row
    tmp="$(mktemp -d)"
    mkfifo "$tmp/ctl" "$tmp/ack"
    run_to "$tmp/bench.csv" perf stat -x, -o "$tmp/perf.txt" -e "$events" --delay=-1 \
        "--control=fifo:$tmp/ctl,$tmp/ack" -- \
        "$BENCH" --n "$n" --algo "$algo" --k "$k" --threads 1 --reps "$reps" --verify \
        --csv-header --perf-ctl "$tmp/ctl" --perf-ack "$tmp/ack"
    # From bench.csv: $1 algo, $4 k, $5 threads, $3 n, $10 reps, $11 median_ms.
    # perf prints the events in the order they were requested.
    row="$(awk -F, -v want="$(awk -F, '{print NF}' <<< "$events")" '
        NR == FNR { if (FNR == 2) head = $1 "," $4 "," $5 "," $3 "," $10 "," $11; next }
        /^#/ || NF < 3 { next }
        $1 !~ /^[0-9.]+$/ { bad = $3 " = " $1 }
        { values = values "," $1; count++ }
        END {
            if (bad != "") { print "perf: " bad > "/dev/stderr"; exit 1 }
            if (head == "" || count != want) {
                print "perf: expected " want " counters, got " count > "/dev/stderr"; exit 1
            }
            print head values
        }' "$tmp/bench.csv" "$tmp/perf.txt")"
    rm -rf "$tmp"
    printf '%s\n' "$row"
}

echo "== [1/4] stream bandwidth at every thread count"
run_to "$OUT/bandwidth_all_threads.csv" "$BANDWIDTH" --threads "$BW_THREADS" \
    --min-size "$BW_SIZE" --max-size "$BW_SIZE" --csv-header

echo "== [2/4] merge path: K = 1024 (AVX2) against K = 1023 (scalar)"
run_to "$OUT/merge_path_k1024_vs_k1023.csv" "$BENCH" --n "$N_BIG" --algo tiled --k 1024,1023 \
    --threads 1 --reps 10 --verify --csv-header

SKIP_REASON=""
if perf_ready; then
    echo "== [3/4] L2 prefetch events, tiled engine"
    {
        echo "algo,k,threads,n,reps,median_ms,l2_pf_miss_l2_hit_l3,l2_pf_miss_l2_l3"
        perf_row "$EVENTS_L2" tiled 64 "$N_SMALL" 20
        perf_row "$EVENTS_L2" tiled 64 "$N_BIG" 20
    } | awk -F, -v OFS=, '
        NR == 1 { print $0, "cache_lines", "l3_hit_pct", "to_memory_pct", "l3_misses_per_elem"; next }
        {
            lines = $4 * 4 / 64 * $5
            printf "%s,%d,%.2f,%.2f,%.5f\n", $0, lines, 100 * $7 / lines, 100 * $8 / lines,
                   $8 / ($4 * $5)
        }' > "$OUT/l2_prefetch_events.csv"

    echo "== [4/4] memory fetches per cache line, each algorithm"
    {
        echo "algo,k,threads,n,reps,median_ms,l2_pf_miss_l2_hit_l3,l2_pf_miss_l2_l3,ls_dmnd_fills_from_sys_mem_io_local"
        for algo in tiled scalar heap nth; do
            perf_row "$EVENTS_L3" "$algo" 64 "$N_BIG" 10
        done
    } | awk -F, -v OFS=, '
        NR == 1 { print $0, "cache_lines", "memory_fetches_per_line"; next }
        {
            lines = $4 * 4 / 64 * $5
            printf "%s,%d,%.3f\n", $0, lines, ($8 + $9) / lines
        }' > "$OUT/l3_by_algorithm.csv"
else
    echo "== [3/4] and [4/4] skipped: $SKIP_REASON"
fi

AVX2="$(sed -n 's/.*avx2=\([01]\).*/\1/p' "$OUT/bench.stderr" | head -1)"
[[ "$AVX2" == "1" ]] || echo "warning: bench_topk reports avx2=$AVX2, so K = 1024 didn't use the AVX2 merge"

cat > "$OUT/README.txt" <<EOF
Follow-up runs for $(basename "$RUN"), made $(date '+%Y-%m-%d %H:%M') on $(hostname) by scripts/followup_amd.sh. bench_topk reported avx2=$AVX2.
Every command is in commands.log.
bandwidth_all_threads.csv: pure stream read of $BW_SIZE at $BW_THREADS threads.
merge_path_k1024_vs_k1023.csv: K = 1023 is not a multiple of 8, so it takes the scalar merge path with the same filter.
l2_prefetch_events.csv: $EVENTS_L2 over the 20 timed reps of the tiled engine, K = 64, one thread. These replace LLC-load-misses, which this CPU does not support. cache_lines = N * 4 / 64 * reps.
l3_by_algorithm.csv: the same perf region, N = $N_BIG, 10 reps, adding ls_dmnd_fills_from_sys.mem_io_local so demand fills from memory count too. memory_fetches_per_line = (l2_pf_miss_l2_l3 + demand fills) / cache_lines; reads only.
EOF

if [[ -n "$SKIP_REASON" ]]; then
    echo "perf rows skipped: $SKIP_REASON" >> "$OUT/README.txt"
    echo "incomplete: $OUT has no perf rows"
    exit 1
fi
echo "done: $OUT"
