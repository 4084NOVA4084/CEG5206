# Top-K benchmark summary
| Field | Value |
|---|---|
| CPU | AMD Ryzen 7 5800H with Radeon Graphics |
| Physical / logical cores | 8 / 16 |
| Caches | L1d 32 KB, L2 512 KB, L3 16 MB |
| perf | {'available': True, 'fifo_roi': True} |
| Preset | full |
| Timestamp | 2026-11-08T02:06:42 |
| Commit | 6b3fd45273d06f021553bcd5d0ad17d296a2e5cd (dirty: False) |
| Source hash | 34d261a70732f7e1 |

Verified configurations: 359 passed, 0 failed, 0 not checked.

## Largest input (N = 32M)

| Implementation | K | P | Median ms | MElem/s | GB/s | % of DRAM stream (<= P) | vs scalar | vs heap | vs nth |
|---|---|---|---|---|---|---|---|---|---|
| OpenMP + tiled SIMD | 8 | 8 | 4.451 | 7539 | 30.2 | 70% | 2.08x | 4.90x | 69.28x |
| Tiled SIMD engine (1 thread) | 8 | 1 | 5.542 | 6054 | 24.2 | 75% | 1.67x | 3.94x | 55.63x |
| OpenMP + hand-written scalar | 8 | 8 | 3.796 | 8840 | 35.4 | 82% | 2.44x | 5.75x | 81.23x |
| Hand-written scalar | 8 | 1 | 9.264 | 3622 | 14.5 | 45% | 1.00x | 2.36x | 33.28x |
| OpenMP + min-heap | 8 | 8 | 3.499 | 9591 | 38.4 | 89% | 2.65x | 6.24x | 88.13x |
| Min-heap baseline | 8 | 1 | 21.822 | 1538 | 6.2 | 19% | 0.42x | 1.00x | 14.13x |
| nth_element baseline | 8 | 1 | 308.324 | 109 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |
| OpenMP + tiled SIMD | 64 | 8 | 4.453 | 7536 | 30.1 | 70% | 2.08x | 4.91x | 69.23x |
| Tiled SIMD engine (1 thread) | 64 | 1 | 5.638 | 5951 | 23.8 | 74% | 1.65x | 3.88x | 54.67x |
| OpenMP + hand-written scalar | 64 | 8 | 3.791 | 8851 | 35.4 | 82% | 2.45x | 5.76x | 81.31x |
| Hand-written scalar | 64 | 1 | 9.281 | 3615 | 14.5 | 45% | 1.00x | 2.35x | 33.21x |
| OpenMP + min-heap | 64 | 8 | 3.503 | 9580 | 38.3 | 89% | 2.65x | 6.24x | 88.01x |
| Min-heap baseline | 64 | 1 | 21.854 | 1535 | 6.1 | 19% | 0.42x | 1.00x | 14.11x |
| nth_element baseline | 64 | 1 | 308.268 | 109 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |
| OpenMP + tiled SIMD | 1024 | 8 | 18.484 | 1815 | 7.3 | 17% | 0.53x | 1.22x | 16.68x |
| Tiled SIMD engine (1 thread) | 1024 | 1 | 26.341 | 1274 | 5.1 | 16% | 0.37x | 0.86x | 11.70x |
| OpenMP + hand-written scalar | 1024 | 8 | 4.042 | 8301 | 33.2 | 77% | 2.44x | 5.60x | 76.26x |
| Hand-written scalar | 1024 | 1 | 9.864 | 3402 | 13.6 | 42% | 1.00x | 2.29x | 31.25x |
| OpenMP + min-heap | 1024 | 8 | 4.007 | 8375 | 33.5 | 77% | 2.46x | 5.65x | 76.95x |
| Min-heap baseline | 1024 | 1 | 22.623 | 1483 | 5.9 | 18% | 0.44x | 1.00x | 13.63x |
| nth_element baseline | 1024 | 1 | 308.290 | 109 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |

## Strong scaling (N = 32M, sequential merge)

| Implementation | K | P | Median ms | Speedup | Efficiency |
|---|---|---|---|---|---|
| OpenMP + min-heap | 64 | 1 | 22.057 | 1.00x | 100% |
| OpenMP + min-heap | 64 | 2 | 11.089 | 1.99x | 99% |
| OpenMP + min-heap | 64 | 4 | 5.786 | 3.81x | 95% |
| OpenMP + min-heap | 64 | 8 | 3.504 | 6.29x | 79% |
| OpenMP + min-heap | 64 | 16 | 5.304 | 4.16x | 26% |
| OpenMP + hand-written scalar | 64 | 1 | 9.240 | 1.00x | 100% |
| OpenMP + hand-written scalar | 64 | 2 | 4.667 | 1.98x | 99% |
| OpenMP + hand-written scalar | 64 | 4 | 3.282 | 2.82x | 70% |
| OpenMP + hand-written scalar | 64 | 8 | 3.797 | 2.43x | 30% |
| OpenMP + hand-written scalar | 64 | 16 | 5.308 | 1.74x | 11% |
| OpenMP + tiled SIMD | 64 | 1 | 5.769 | 1.00x | 100% |
| OpenMP + tiled SIMD | 64 | 2 | 3.583 | 1.61x | 81% |
| OpenMP + tiled SIMD | 64 | 4 | 3.321 | 1.74x | 43% |
| OpenMP + tiled SIMD | 64 | 8 | 4.451 | 1.30x | 16% |
| OpenMP + tiled SIMD | 64 | 16 | 5.553 | 1.04x | 6% |
| OpenMP + tiled SIMD | 1024 | 1 | 26.764 | 1.00x | 100% |
| OpenMP + tiled SIMD | 1024 | 2 | 22.668 | 1.18x | 59% |
| OpenMP + tiled SIMD | 1024 | 4 | 20.485 | 1.31x | 33% |
| OpenMP + tiled SIMD | 1024 | 8 | 18.425 | 1.45x | 18% |
| OpenMP + tiled SIMD | 1024 | 16 | 17.823 | 1.50x | 9% |

## Merge-phase cost (µs, omp_tiled)

| K | P | Sequential K-way | Two-level | Pairwise tree |
|---|---|---|---|---|
| 64 | 2 | 1.0 | 1.3 | 1.1 |
| 64 | 4 | 2.9 | 2.8 | 2.6 |
| 64 | 8 | 3.3 | 3.8 | 1.8 |
| 64 | 16 | 4.8 | 4.9 | 5.0 |
| 1024 | 2 | 2.1 | 2.2 | 2.1 |
| 1024 | 4 | 16.9 | 4.9 | 4.0 |
| 1024 | 8 | 17.7 | 32.4 | 6.4 |
| 1024 | 16 | 24.7 | 36.2 | 11.4 |

## Best tile size

| K | Best B | MElem/s | All B: MElem/s |
|---|---|---|---|
| 16 | 8192 | 6585 | 128: 5251 / 256: 6055 / 512: 6158 / 1024: 6289 / 2048: 6430 / 4096: 6471 / 8192: 6585 / 16384: 6545 |
| 64 | 16384 | 6137 | 128: 5512 / 256: 5713 / 512: 5826 / 1024: 5895 / 2048: 5997 / 4096: 6072 / 8192: 6127 / 16384: 6137 |
| 256 | 16384 | 3208 | 128: 3031 / 256: 3099 / 512: 3115 / 1024: 3139 / 2048: 3179 / 4096: 3195 / 8192: 3200 / 16384: 3208 |

## Input distributions (MElem/s)

| Distribution | OpenMP + tiled SIMD | Tiled SIMD engine (1 thread) | Hand-written scalar | Min-heap baseline |
|---|---|---|---|---|
| ascending | 397 | 54 | 52 | 55 |
| descending | 9441 | 6281 | 3707 | 1533 |
| duplicates | 9338 | 6190 | 3747 | 1513 |
| gaussian | 9310 | 5854 | 3695 | 1508 |
| special | 9248 | 6144 | 3733 | 1508 |
| uniform | 9240 | 5857 | 3719 | 1528 |

## Bandwidth ceilings (stream max)

| Threads | Peak in-cache GB/s (<= L2) | DRAM GB/s (largest size) |
|---|---|---|
| 1 | 205.9 | 32.1 |
| 2 | 407.8 | 43.2 |
| 4 | 782.5 | 41.8 |
| 8 | 970.1 | 27.3 |
| 16 | 842.4 | 24.2 |

Theoretical DRAM peak: 51.2 GB/s

## Hardware counters (per element, timed region only)

| Algo | P | K | ROI | instr | branch-miss | L1d miss | LLC miss | IPC |
|---|---|---|---|---|---|---|---|---|
| heap | 1 | 64 | fifo | 13.0017 | 0.0001 | 0.0626 | nan | 4.82 |
| scalar | 1 | 64 | fifo | 6.0012 | 0.0001 | 0.0626 | nan | 5.24 |
| nth | 1 | 64 | fifo | 14.9419 | 0.9586 | 0.2645 | nan | 0.46 |
| tiled | 1 | 64 | fifo | 1.1612 | 0.0010 | 0.0628 | nan | 1.63 |
| omp_tiled | 1 | 64 | fifo | 1.1608 | 0.0010 | 0.0625 | nan | 1.63 |
| omp_tiled | 8 | 64 | fifo | 1.2240 | 0.0011 | 0.0629 | nan | 0.29 |
| omp_heap | 8 | 64 | fifo | 13.0460 | 0.0005 | 0.0631 | nan | 3.92 |
| omp_scalar | 8 | 64 | fifo | 6.0143 | 0.0005 | 0.0619 | nan | 1.47 |
| heap | 1 | 1024 | fifo | 13.0050 | 0.0026 | 0.0641 | nan | 4.65 |
| scalar | 1 | 1024 | fifo | 6.0677 | 0.0021 | 0.0634 | nan | 4.98 |
| nth | 1 | 1024 | fifo | 15.0011 | 0.9586 | 0.2702 | nan | 0.46 |
| tiled | 1 | 1024 | fifo | 3.0701 | 0.0015 | 0.0637 | nan | 0.92 |
| omp_tiled | 1 | 1024 | fifo | 3.0696 | 0.0015 | 0.0636 | nan | 0.92 |
| omp_tiled | 8 | 1024 | fifo | 12.9610 | 0.0043 | 0.0657 | nan | 0.75 |
| omp_heap | 8 | 1024 | fifo | 13.4283 | 0.0147 | 0.0658 | nan | 3.45 |
| omp_scalar | 8 | 1024 | fifo | 6.3173 | 0.0138 | 0.0668 | nan | 1.59 |
