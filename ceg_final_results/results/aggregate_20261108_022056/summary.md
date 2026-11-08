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
| Median of runs | 20261108_020610_full, 20261108_021044_full, 20261108_021518_full |

Verified configurations: 359 passed, 0 failed, 0 not checked.

## Largest input (N = 32M)

| Implementation | K | P | Median ms | MElem/s | GB/s | % of DRAM stream (<= P) | vs scalar | vs heap | vs nth |
|---|---|---|---|---|---|---|---|---|---|
| OpenMP + tiled SIMD | 8 | 8 | 4.447 | 7545 | 30.2 | 70% | 2.08x | 4.91x | 69.49x |
| Tiled SIMD engine (1 thread) | 8 | 1 | 5.542 | 6054 | 24.2 | 75% | 1.67x | 3.94x | 55.77x |
| OpenMP + hand-written scalar | 8 | 8 | 3.796 | 8840 | 35.4 | 82% | 2.44x | 5.75x | 81.43x |
| Hand-written scalar | 8 | 1 | 9.264 | 3622 | 14.5 | 45% | 1.00x | 2.36x | 33.36x |
| OpenMP + min-heap | 8 | 8 | 3.495 | 9601 | 38.4 | 89% | 2.65x | 6.24x | 88.43x |
| Min-heap baseline | 8 | 1 | 21.824 | 1537 | 6.1 | 19% | 0.42x | 1.00x | 14.16x |
| nth_element baseline | 8 | 1 | 309.063 | 109 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |
| OpenMP + tiled SIMD | 64 | 8 | 4.453 | 7536 | 30.1 | 70% | 2.08x | 4.91x | 69.30x |
| Tiled SIMD engine (1 thread) | 64 | 1 | 5.643 | 5946 | 23.8 | 74% | 1.64x | 3.87x | 54.69x |
| OpenMP + hand-written scalar | 64 | 8 | 3.794 | 8843 | 35.4 | 82% | 2.45x | 5.76x | 81.33x |
| Hand-written scalar | 64 | 1 | 9.281 | 3615 | 14.5 | 45% | 1.00x | 2.35x | 33.25x |
| OpenMP + min-heap | 64 | 8 | 3.503 | 9580 | 38.3 | 88% | 2.65x | 6.24x | 88.10x |
| Min-heap baseline | 64 | 1 | 21.854 | 1535 | 6.1 | 19% | 0.42x | 1.00x | 14.12x |
| nth_element baseline | 64 | 1 | 308.577 | 109 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |
| OpenMP + tiled SIMD | 1024 | 8 | 18.491 | 1815 | 7.3 | 17% | 0.53x | 1.22x | 16.69x |
| Tiled SIMD engine (1 thread) | 1024 | 1 | 26.363 | 1273 | 5.1 | 16% | 0.37x | 0.86x | 11.71x |
| OpenMP + hand-written scalar | 1024 | 8 | 4.037 | 8312 | 33.2 | 77% | 2.44x | 5.60x | 76.44x |
| Hand-written scalar | 1024 | 1 | 9.861 | 3403 | 13.6 | 42% | 1.00x | 2.29x | 31.29x |
| OpenMP + min-heap | 1024 | 8 | 4.007 | 8375 | 33.5 | 77% | 2.46x | 5.65x | 77.02x |
| Min-heap baseline | 1024 | 1 | 22.623 | 1483 | 5.9 | 18% | 0.44x | 1.00x | 13.64x |
| nth_element baseline | 1024 | 1 | 308.590 | 109 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |

## Strong scaling (N = 32M, sequential merge)

| Implementation | K | P | Median ms | Speedup | Efficiency |
|---|---|---|---|---|---|
| OpenMP + min-heap | 64 | 1 | 22.108 | 1.00x | 100% |
| OpenMP + min-heap | 64 | 2 | 11.089 | 1.99x | 100% |
| OpenMP + min-heap | 64 | 4 | 5.786 | 3.82x | 96% |
| OpenMP + min-heap | 64 | 8 | 3.504 | 6.31x | 79% |
| OpenMP + min-heap | 64 | 16 | 5.306 | 4.17x | 26% |
| OpenMP + hand-written scalar | 64 | 1 | 9.240 | 1.00x | 100% |
| OpenMP + hand-written scalar | 64 | 2 | 4.656 | 1.98x | 99% |
| OpenMP + hand-written scalar | 64 | 4 | 3.275 | 2.82x | 71% |
| OpenMP + hand-written scalar | 64 | 8 | 3.797 | 2.43x | 30% |
| OpenMP + hand-written scalar | 64 | 16 | 5.309 | 1.74x | 11% |
| OpenMP + tiled SIMD | 64 | 1 | 5.813 | 1.00x | 100% |
| OpenMP + tiled SIMD | 64 | 2 | 3.592 | 1.62x | 81% |
| OpenMP + tiled SIMD | 64 | 4 | 3.358 | 1.73x | 43% |
| OpenMP + tiled SIMD | 64 | 8 | 4.452 | 1.31x | 16% |
| OpenMP + tiled SIMD | 64 | 16 | 5.553 | 1.05x | 7% |
| OpenMP + tiled SIMD | 1024 | 1 | 26.764 | 1.00x | 100% |
| OpenMP + tiled SIMD | 1024 | 2 | 22.571 | 1.19x | 59% |
| OpenMP + tiled SIMD | 1024 | 4 | 20.485 | 1.31x | 33% |
| OpenMP + tiled SIMD | 1024 | 8 | 18.425 | 1.45x | 18% |
| OpenMP + tiled SIMD | 1024 | 16 | 17.823 | 1.50x | 9% |

## Merge-phase cost (µs, omp_tiled)

| K | P | Sequential K-way | Two-level | Pairwise tree |
|---|---|---|---|---|
| 64 | 2 | 0.9 | 1.2 | 0.8 |
| 64 | 4 | 3.4 | 2.3 | 2.3 |
| 64 | 8 | 3.5 | 3.9 | 1.8 |
| 64 | 16 | 5.2 | 5.3 | 4.6 |
| 1024 | 2 | 2.0 | 2.3 | 1.8 |
| 1024 | 4 | 16.9 | 4.9 | 4.3 |
| 1024 | 8 | 17.7 | 32.1 | 7.6 |
| 1024 | 16 | 24.7 | 36.8 | 11.9 |

## Best tile size

| K | Best B | MElem/s | All B: MElem/s |
|---|---|---|---|
| 16 | 16384 | 6550 | 128: 5251 / 256: 5984 / 512: 6107 / 1024: 6281 / 2048: 6427 / 4096: 6307 / 8192: 6542 / 16384: 6550 |
| 64 | 16384 | 6120 | 128: 5496 / 256: 5713 / 512: 5826 / 1024: 5876 / 2048: 5997 / 4096: 5996 / 8192: 6087 / 16384: 6120 |
| 256 | 16384 | 3203 | 128: 3028 / 256: 3094 / 512: 3111 / 1024: 3138 / 2048: 3176 / 4096: 3195 / 8192: 3196 / 16384: 3203 |

## Input distributions (MElem/s)

| Distribution | OpenMP + tiled SIMD | Tiled SIMD engine (1 thread) | Hand-written scalar | Min-heap baseline |
|---|---|---|---|---|
| ascending | 397 | 54 | 52 | 55 |
| descending | 9366 | 6251 | 3748 | 1533 |
| duplicates | 9358 | 6190 | 3747 | 1512 |
| gaussian | 9283 | 5804 | 3695 | 1505 |
| special | 9228 | 6132 | 3733 | 1509 |
| uniform | 9291 | 5830 | 3718 | 1524 |

## Bandwidth ceilings (stream max)

| Threads | Peak in-cache GB/s (<= L2) | DRAM GB/s (largest size) |
|---|---|---|
| 1 | 205.9 | 32.1 |
| 2 | 407.8 | 43.3 |
| 4 | 775.8 | 41.9 |
| 8 | 970.1 | 27.3 |
| 16 | 839.2 | 24.2 |

Theoretical DRAM peak: 51.2 GB/s

## Hardware counters (per element, timed region only)

| Algo | P | K | ROI | instr | branch-miss | L1d miss | LLC miss | IPC |
|---|---|---|---|---|---|---|---|---|
| heap | 1 | 64 | fifo | 13.0067 | 0.0001 | 0.0626 | nan | 4.82 |
| scalar | 1 | 64 | fifo | 6.0031 | 0.0001 | 0.0626 | nan | 5.25 |
| nth | 1 | 64 | fifo | 14.9887 | 0.9586 | 0.2589 | nan | 0.46 |
| tiled | 1 | 64 | fifo | 1.1579 | 0.0010 | 0.0627 | nan | 1.62 |
| omp_tiled | 1 | 64 | fifo | 1.1576 | 0.0010 | 0.0627 | nan | 1.63 |
| omp_tiled | 8 | 64 | fifo | 1.2262 | 0.0011 | 0.0629 | nan | 0.29 |
| omp_heap | 8 | 64 | fifo | 13.0460 | 0.0005 | 0.0627 | nan | 3.92 |
| omp_scalar | 8 | 64 | fifo | 6.0143 | 0.0005 | 0.0625 | nan | 1.54 |
| heap | 1 | 1024 | fifo | 13.0081 | 0.0021 | 0.0637 | nan | 4.66 |
| scalar | 1 | 1024 | fifo | 6.0677 | 0.0021 | 0.0634 | nan | 4.98 |
| nth | 1 | 1024 | fifo | 14.9969 | 0.9586 | 0.2702 | nan | 0.46 |
| tiled | 1 | 1024 | fifo | 3.0701 | 0.0015 | 0.0635 | nan | 0.92 |
| omp_tiled | 1 | 1024 | fifo | 3.0738 | 0.0015 | 0.0631 | nan | 0.92 |
| omp_tiled | 8 | 1024 | fifo | 12.9610 | 0.0043 | 0.0655 | nan | 0.75 |
| omp_heap | 8 | 1024 | fifo | 13.4466 | 0.0157 | 0.0658 | nan | 3.53 |
| omp_scalar | 8 | 1024 | fifo | 6.3458 | 0.0138 | 0.0656 | nan | 1.64 |
