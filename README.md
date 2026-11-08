# Parallel SIMD Top-K Selection

CEG5206 group project. It finds the K largest of N floats on a multi-core CPU, and measures what each optimisation adds.

The pipeline has four layers, one per team member:

| Folder | Member | Task |
|---|---|---|
| [`topk_project_member_a`](topk_project_member_a) | POON WEI XUAN | Scalar baselines, FP64 oracle, data generators, `run_validation` |
| [`topk_project_member_b`](topk_project_member_b) | SEBASTIAN SOEWANTO | AVX2 bitonic merge kernel and assembly audit |
| [`topk_project_member_c`](topk_project_member_c) | WANG YUDA | Cache-tiled engine with a broadcast threshold filter |
| [`topk_project_member_d`](topk_project_member_d) | MA XUDONG | OpenMP wrapper, benchmarks, `perf` profiling and plots |

## Results

Measured on an AMD Ryzen 7 5800H laptop (8 cores, dual-channel DDR4-3200) at N = 32M floats. Each number is the median of three full runs.

- At K = 64 the full pipeline, the OpenMP wrapper on the tiled engine with 4 threads, takes 3.33 ms. That is 40.3 GB/s: 79% of the theoretical 51.2 GB/s and 93% of the best measured streaming read (43.3 GB/s). It is 6.6× faster than the min-heap, 2.8× faster than a hand-written scalar loop and 93× faster than `nth_element`.
- On one thread the tiled engine is 1.64× faster than the scalar loop. The gain comes from the vector threshold filter, which discards most blocks before they reach the merge.
- With several threads, memory bandwidth is the limit. At 4 threads the OpenMP scalar loop is as fast as the tiled engine (3.28 ms), and at 8 threads the min-heap is the fastest of the three.
- Large K favours the scalar loop. On one thread at N = 8M, the tiled engine falls behind it from K = 256 and behind the min-heap from K = 512.

The data behind these numbers and the report figures are in [`ceg_final_results/results`](ceg_final_results/results):

| Directory | Contents |
|---|---|
| `aggregate_20261108_022056/` | Medians of the three runs: `summary.md`, the CSVs, `figures/`, and the run-to-run spread in `variation.md` |
| `20261108_020610_full/`, `20261108_021044_full/`, `20261108_021518_full/` | The three full runs, each with its own `summary.md`, `validation.log` and `system_info.json` |
| `20261108_021518_full/followup/` | AMD cache events, the streaming read at every thread count, AVX2 merge against the scalar merge |
| `20261108_021518_full/ablation/` | The optimisation ladder, in `ladder.md` |

All three runs use commit `6b3fd45` with a clean tree, the `performance` governor and mains power. Each verifies all 359 timed configurations.

## Requirements

- A C++17 compiler with OpenMP, and GNU make. GCC 9 or newer works.
- An AVX2 CPU for the SIMD path. Other CPUs build and pass the tests through a portable path.
- Python 3.7 or newer with matplotlib, for benchmarks and figures.
- Linux `perf` for hardware counters. Without it the counter sweep is skipped.

## Build and test

All commands run from `topk_project_member_d`.

```bash
cd topk_project_member_d
make -j
make test
```

`make test` runs `run_validation` on every implementation, then checks each kernel and merge strategy against the FP64 oracle at several thread counts.

## Run

```bash
./scripts/run_all.sh quick
```

This one command builds, validates, benchmarks and plots. Results go to `results/<timestamp>_<preset>/`, which git ignores: start with `summary.md`, then `figures/`. `system_info.json` records the machine and the exact code version.

## Reproduce the report data

On Linux, on mains power:

```bash
sudo sysctl kernel.perf_event_paranoid=1
sudo cpupower frequency-set -g performance
for i in 1 2 3; do ./scripts/run_all.sh full --dram-peak-gbs 51.2 --peak-gops 512; sleep 120; done
python3 scripts/aggregate_runs.py $(ls -d results/*_full | tail -3)
python3 scripts/plot_results.py "$(ls -td results/aggregate_* | head -1)" --format png,pdf
./scripts/followup_amd.sh
python3 scripts/run_ablation.py
```

This produces the same layout as `ceg_final_results/results`. `followup_amd.sh` and `run_ablation.py` write into the newest full run, and the follow-up events are specific to AMD Zen.

`51.2` is the theoretical DRAM bandwidth of dual-channel DDR4-3200 (2 × 3200 MT/s × 8 B). `512` is the compute roof for the roofline: 8 cores × 4.0 GHz × 8 lanes × 2 vector compares per cycle. The clock and the compare rate are estimates, not measurements.

To redraw the figures from the committed data without rerunning, work on a copy, because `plot_results.py` writes `figures/` and `summary.md` into the directory it reads:

```bash
cp -R ../ceg_final_results/results/aggregate_20261108_022056 /tmp/
python3 scripts/plot_results.py /tmp/aggregate_20261108_022056 --format png,pdf
```
