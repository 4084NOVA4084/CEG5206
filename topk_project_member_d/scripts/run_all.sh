#!/usr/bin/env bash
# Usage: ./scripts/run_all.sh [quick|full] [extra run_benchmarks.py args]
# On the Linux box first:
#   sudo sysctl kernel.perf_event_paranoid=1     # 2 is enough for user-space counters
#   sudo cpupower frequency-set -g performance   # pinned clocks, less noise
#   ./scripts/run_all.sh full --dram-peak-gbs <channels x MT/s x 8 / 1000>
set -euo pipefail

PRESET="${1:-quick}"
shift || true
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
cd "$HERE"

# Fail now, not after minutes of benchmarking, if plotting can't work.
"$PYTHON" -c 'import sys; sys.exit(sys.version_info < (3, 7))' 2>/dev/null \
    || { echo "need Python >= 3.7 (set PYTHON=/path/to/python)"; exit 1; }
"$PYTHON" -c 'import matplotlib' 2>/dev/null \
    || { echo "matplotlib missing for $PYTHON: pip install matplotlib (or set PYTHON=...)"; exit 1; }

export OMP_PROC_BIND="${OMP_PROC_BIND:-close}"
export OMP_PLACES="${OMP_PLACES:-cores}"

echo "== [1/4] build"
make -j all

echo "== [2/4] validation (race freedom + determinism across thread counts)"
if [[ "$PRESET" == "quick" ]]; then
    ./build/test_parallel_topk --quick
else
    ./build/test_parallel_topk
fi

OUT="results/$(date +%Y%m%d_%H%M%S)_${PRESET}"
echo "== [3/4] benchmarks -> $OUT"
"$PYTHON" scripts/run_benchmarks.py --preset "$PRESET" --no-build --out "$OUT" "$@"

echo "== [4/4] figures"
"$PYTHON" scripts/plot_results.py "$OUT" --format png,pdf

echo "done: $OUT/summary.md and $OUT/figures/"
