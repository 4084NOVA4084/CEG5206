# Top-K benchmark summary
| Field | Value |
|---|---|
| CPU | AMD Ryzen 7 5800H with Radeon Graphics |
| Physical / logical cores | 8 / 16 |
| Caches | L1d 32 KB, L2 512 KB, L3 16 MB |
| perf | {'available': True, 'fifo_roi': True} |
| Preset | full |
| Timestamp | 2026-11-08T02:11:15 |
| Commit | 6b3fd45273d06f021553bcd5d0ad17d296a2e5cd (dirty: False) |
| Source hash | 34d261a70732f7e1 |

Verified configurations: 359 passed, 0 failed, 0 not checked.

## Largest input (N = 32M)

| Implementation | K | P | Median ms | MElem/s | GB/s | % of DRAM stream (<= P) | vs scalar | vs heap | vs nth |
|---|---|---|---|---|---|---|---|---|---|
| OpenMP + tiled SIMD | 8 | 8 | 4.447 | 7545 | 30.2 | 70% | 2.08x | 4.98x | 69.56x |
| Tiled SIMD engine (1 thread) | 8 | 1 | 5.543 | 6053 | 24.2 | 75% | 1.67x | 3.99x | 55.81x |
| OpenMP + hand-written scalar | 8 | 8 | 3.793 | 8847 | 35.4 | 82% | 2.44x | 5.84x | 81.56x |
| Hand-written scalar | 8 | 1 | 9.271 | 3619 | 14.5 | 45% | 1.00x | 2.39x | 33.37x |
| OpenMP + min-heap | 8 | 8 | 3.493 | 9605 | 38.4 | 89% | 2.65x | 6.34x | 88.55x |
| Min-heap baseline | 8 | 1 | 22.138 | 1516 | 6.1 | 19% | 0.42x | 1.00x | 13.97x |
| nth_element baseline | 8 | 1 | 309.344 | 108 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |
| OpenMP + tiled SIMD | 64 | 8 | 4.452 | 7538 | 30.1 | 70% | 2.09x | 4.97x | 69.48x |
| Tiled SIMD engine (1 thread) | 64 | 1 | 5.653 | 5936 | 23.7 | 74% | 1.64x | 3.91x | 54.72x |
| OpenMP + hand-written scalar | 64 | 8 | 3.794 | 8843 | 35.4 | 82% | 2.45x | 5.83x | 81.52x |
| Hand-written scalar | 64 | 1 | 9.286 | 3613 | 14.5 | 45% | 1.00x | 2.38x | 33.31x |
| OpenMP + min-heap | 64 | 8 | 3.505 | 9574 | 38.3 | 88% | 2.65x | 6.31x | 88.26x |
| Min-heap baseline | 64 | 1 | 22.108 | 1518 | 6.1 | 19% | 0.42x | 1.00x | 13.99x |
| nth_element baseline | 64 | 1 | 309.305 | 108 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |
| OpenMP + tiled SIMD | 1024 | 8 | 18.554 | 1808 | 7.2 | 17% | 0.53x | 1.23x | 16.68x |
| Tiled SIMD engine (1 thread) | 1024 | 1 | 26.674 | 1258 | 5.0 | 16% | 0.37x | 0.86x | 11.60x |
| OpenMP + hand-written scalar | 1024 | 8 | 4.032 | 8322 | 33.3 | 77% | 2.45x | 5.66x | 76.74x |
| Hand-written scalar | 1024 | 1 | 9.861 | 3403 | 13.6 | 42% | 1.00x | 2.32x | 31.38x |
| OpenMP + min-heap | 1024 | 8 | 4.006 | 8375 | 33.5 | 77% | 2.46x | 5.70x | 77.23x |
| Min-heap baseline | 1024 | 1 | 22.840 | 1469 | 5.9 | 18% | 0.43x | 1.00x | 13.55x |
| nth_element baseline | 1024 | 1 | 309.408 | 108 | 0.4 | 1% | 0.03x | 0.07x | 1.00x |

## Strong scaling (N = 32M, sequential merge)

| Implementation | K | P | Median ms | Speedup | Efficiency |
|---|---|---|---|---|---|
| OpenMP + min-heap | 64 | 1 | 22.334 | 1.00x | 100% |
| OpenMP + min-heap | 64 | 2 | 11.268 | 1.98x | 99% |
| OpenMP + min-heap | 64 | 4 | 5.974 | 3.74x | 93% |
| OpenMP + min-heap | 64 | 8 | 3.504 | 6.37x | 80% |
| OpenMP + min-heap | 64 | 16 | 5.316 | 4.20x | 26% |
| OpenMP + hand-written scalar | 64 | 1 | 9.336 | 1.00x | 100% |
| OpenMP + hand-written scalar | 64 | 2 | 4.656 | 2.01x | 100% |
| OpenMP + hand-written scalar | 64 | 4 | 3.273 | 2.85x | 71% |
| OpenMP + hand-written scalar | 64 | 8 | 3.793 | 2.46x | 31% |
| OpenMP + hand-written scalar | 64 | 16 | 5.316 | 1.76x | 11% |
| OpenMP + tiled SIMD | 64 | 1 | 5.813 | 1.00x | 100% |
| OpenMP + tiled SIMD | 64 | 2 | 3.592 | 1.62x | 81% |
| OpenMP + tiled SIMD | 64 | 4 | 3.390 | 1.71x | 43% |
| OpenMP + tiled SIMD | 64 | 8 | 4.452 | 1.31x | 16% |
| OpenMP + tiled SIMD | 64 | 16 | 5.536 | 1.05x | 7% |
| OpenMP + tiled SIMD | 1024 | 1 | 26.769 | 1.00x | 100% |
| OpenMP + tiled SIMD | 1024 | 2 | 22.549 | 1.19x | 59% |
| OpenMP + tiled SIMD | 1024 | 4 | 20.510 | 1.31x | 33% |
| OpenMP + tiled SIMD | 1024 | 8 | 18.455 | 1.45x | 18% |
| OpenMP + tiled SIMD | 1024 | 16 | 17.870 | 1.50x | 9% |

## Merge-phase cost (µs, omp_tiled)

| K | P | Sequential K-way | Two-level | Pairwise tree |
|---|---|---|---|---|
| 64 | 2 | 0.9 | 1.1 | 0.8 |
| 64 | 4 | 3.4 | 2.3 | 1.5 |
| 64 | 8 | 3.5 | 3.9 | 1.8 |
| 64 | 16 | 5.2 | 5.3 | 4.1 |
| 1024 | 2 | 2.0 | 2.3 | 1.7 |
| 1024 | 4 | 17.3 | 4.7 | 4.3 |
| 1024 | 8 | 17.9 | 32.1 | 8.8 |
| 1024 | 16 | 24.7 | 36.8 | 12.7 |

## Best tile size

| K | Best B | MElem/s | All B: MElem/s |
|---|---|---|---|
| 16 | 16384 | 6550 | 128: 5231 / 256: 5955 / 512: 6107 / 1024: 6247 / 2048: 6413 / 4096: 6275 / 8192: 6516 / 16384: 6550 |
| 64 | 16384 | 6120 | 128: 5475 / 256: 5706 / 512: 5799 / 1024: 5848 / 2048: 5981 / 4096: 5993 / 8192: 6087 / 16384: 6120 |
| 256 | 8192 | 3189 | 128: 3021 / 256: 3085 / 512: 3102 / 1024: 3131 / 2048: 3165 / 4096: 3188 / 8192: 3189 / 16384: 3189 |

## Input distributions (MElem/s)

| Distribution | OpenMP + tiled SIMD | Tiled SIMD engine (1 thread) | Hand-written scalar | Min-heap baseline |
|---|---|---|---|---|
| ascending | 396 | 53 | 52 | 55 |
| descending | 9089 | 6243 | 3748 | 1511 |
| duplicates | 9378 | 6178 | 3742 | 1512 |
| gaussian | 9283 | 5800 | 3687 | 1487 |
| special | 9176 | 6100 | 3745 | 1509 |
| uniform | 9291 | 5794 | 3718 | 1498 |

## Bandwidth ceilings (stream max)

| Threads | Peak in-cache GB/s (<= L2) | DRAM GB/s (largest size) |
|---|---|---|
| 1 | 199.2 | 32.1 |
| 2 | 400.9 | 43.3 |
| 4 | 775.8 | 41.9 |
| 8 | 964.1 | 27.3 |
| 16 | 833.7 | 24.2 |

Theoretical DRAM peak: 51.2 GB/s

## Hardware counters (per element, timed region only)

| Algo | P | K | ROI | instr | branch-miss | L1d miss | LLC miss | IPC |
|---|---|---|---|---|---|---|---|---|
| heap | 1 | 64 | fifo | 13.0067 | 0.0001 | 0.0626 | nan | 4.82 |
| scalar | 1 | 64 | fifo | 6.0041 | 0.0001 | 0.0626 | nan | 5.27 |
| nth | 1 | 64 | fifo | 14.9887 | 0.9586 | 0.2589 | nan | 0.46 |
| tiled | 1 | 64 | fifo | 1.1577 | 0.0010 | 0.0627 | nan | 1.61 |
| omp_tiled | 1 | 64 | fifo | 1.1576 | 0.0010 | 0.0632 | nan | 1.61 |
| omp_tiled | 8 | 64 | fifo | 1.2262 | 0.0011 | 0.0629 | nan | 0.29 |
| omp_heap | 8 | 64 | fifo | 13.3063 | 0.0005 | 0.0616 | nan | 3.53 |
| omp_scalar | 8 | 64 | fifo | 6.0919 | 0.0005 | 0.0625 | nan | 1.56 |
| heap | 1 | 1024 | fifo | 13.0208 | 0.0021 | 0.0635 | nan | 4.66 |
| scalar | 1 | 1024 | fifo | 6.0759 | 0.0020 | 0.0633 | nan | 4.97 |
| nth | 1 | 1024 | fifo | 14.9969 | 0.9581 | 0.2682 | nan | 0.46 |
| tiled | 1 | 1024 | fifo | 3.0754 | 0.0015 | 0.0634 | nan | 0.92 |
| omp_tiled | 1 | 1024 | fifo | 3.0738 | 0.0015 | 0.0631 | nan | 0.92 |
| omp_tiled | 8 | 1024 | fifo | 12.9564 | 0.0043 | 0.0652 | nan | 0.75 |
| omp_heap | 8 | 1024 | fifo | 13.4791 | 0.0157 | 0.0654 | nan | 3.57 |
| omp_scalar | 8 | 1024 | fifo | 6.3762 | 0.0138 | 0.0649 | nan | 1.67 |
