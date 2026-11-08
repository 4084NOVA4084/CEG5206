# Variation across runs

| Run | CPU | Commit | Dirty | Source hash | Governor | Mains | Rows |
|---|---|---|---|---|---|---|---|
| 20261108_020610_full | AMD Ryzen 7 5800H with Radeon Graphics | 6b3fd45273 | no | 34d261a707 | performance | yes | 359 |
| 20261108_021044_full | AMD Ryzen 7 5800H with Radeon Graphics | 6b3fd45273 | no | 34d261a707 | performance | yes | 359 |
| 20261108_021518_full | AMD Ryzen 7 5800H with Radeon Graphics | 6b3fd45273 | no | 34d261a707 | performance | yes | 359 |

## Run-to-run spread

Across 359 configurations: median 0.7%, 90th percentile 4.3%, maximum 94.3%.

Noisiest configurations:

| Sweep | Configuration | Median ms | Range ms | Spread |
|---|---|---|---|---|
| size | omp_scalar N=524288 K=8 P=8 | 0.027 | 0.026 to 0.051 | 94.3% |
| size | omp_heap N=524288 K=8 P=8 | 0.049 | 0.049 to 0.086 | 74.9% |
| size | omp_heap N=524288 K=64 P=8 | 0.061 | 0.061 to 0.098 | 61.3% |
| size | omp_scalar N=524288 K=64 P=8 | 0.035 | 0.034 to 0.052 | 48.5% |
| size | omp_scalar N=1048576 K=8 P=8 | 0.051 | 0.049 to 0.059 | 20.0% |
| size | omp_tiled N=262144 K=8 P=8 | 0.007 | 0.007 to 0.008 | 19.5% |
| size | omp_scalar N=262144 K=8 P=8 | 0.016 | 0.015 to 0.018 | 18.9% |
| size | omp_heap N=524288 K=1024 P=8 | 0.434 | 0.433 to 0.511 | 18.1% |
| size | omp_scalar N=524288 K=1024 P=8 | 0.325 | 0.324 to 0.375 | 15.9% |
| size | tiled N=4194304 K=8 P=1 | 0.627 | 0.582 to 0.653 | 11.4% |

## Best configuration at N = 33554432

Each run's fastest row per algorithm, over every P and merge strategy in the size and scaling sweeps.

| K | Algorithm | Median ms | Range ms | Winner in each run |
|---|---|---|---|---|
| 8 | omp_tiled | 4.447 | 4.445 to 4.451 | P=8/seq, P=8/seq, P=8/seq |
| 8 | tiled | 5.542 | 5.537 to 5.543 | P=1, P=1, P=1 |
| 8 | omp_scalar | 3.796 | 3.793 to 3.796 | P=8/seq, P=8/seq, P=8/seq |
| 8 | scalar | 9.264 | 9.261 to 9.271 | P=1, P=1, P=1 |
| 8 | omp_heap | 3.495 | 3.493 to 3.499 | P=8/seq, P=8/seq, P=8/seq |
| 8 | heap | 21.824 | 21.822 to 22.138 | P=1, P=1, P=1 |
| 8 | nth | 309.063 | 308.324 to 309.344 | P=1, P=1, P=1 |
| 64 | omp_tiled | 3.323 | 3.321 to 3.333 | P=4/seq, P=4/two, P=4/tree |
| 64 | tiled | 5.643 | 5.638 to 5.653 | P=1, P=1, P=1 |
| 64 | omp_scalar | 3.275 | 3.273 to 3.282 | P=4/seq, P=4/seq, P=4/seq |
| 64 | scalar | 9.281 | 9.278 to 9.286 | P=1, P=1, P=1 |
| 64 | omp_heap | 3.503 | 3.499 to 3.504 | P=8/seq, P=8/seq, P=8/seq |
| 64 | heap | 21.854 | 21.840 to 22.108 | P=1, P=1, P=1 |
| 64 | nth | 308.577 | 308.268 to 309.305 | P=1, P=1, P=1 |
| 1024 | omp_tiled | 17.823 | 17.801 to 17.870 | P=16/seq, P=16/seq, P=16/tree |
| 1024 | tiled | 26.363 | 26.341 to 26.674 | P=1, P=1, P=1 |
| 1024 | omp_scalar | 4.037 | 4.032 to 4.042 | P=8/seq, P=8/seq, P=8/seq |
| 1024 | scalar | 9.861 | 9.861 to 9.864 | P=1, P=1, P=1 |
| 1024 | omp_heap | 4.007 | 4.006 to 4.016 | P=8/seq, P=8/seq, P=8/seq |
| 1024 | heap | 22.623 | 22.620 to 22.840 | P=1, P=1, P=1 |
| 1024 | nth | 308.590 | 308.290 to 309.408 | P=1, P=1, P=1 |

## Key speedups

Best row against best row, computed inside each run, then the median and range. Each omp_ side uses its own fastest P, so the two sides can run different thread counts.

| K | Comparison | Median | Range |
|---|---|---|---|
| 8 | tiled vs scalar | 1.67x | 1.67x to 1.67x |
| 8 | tiled vs heap | 3.94x | 3.94x to 3.99x |
| 8 | omp_tiled vs tiled | 1.25x | 1.25x to 1.25x |
| 8 | omp_tiled vs omp_scalar | 0.85x | 0.85x to 0.85x |
| 8 | omp_tiled vs omp_heap | 0.79x | 0.79x to 0.79x |
| 8 | omp_tiled vs scalar | 2.08x | 2.08x to 2.08x |
| 8 | omp_tiled vs heap | 4.91x | 4.90x to 4.98x |
| 64 | tiled vs scalar | 1.64x | 1.64x to 1.65x |
| 64 | tiled vs heap | 3.88x | 3.87x to 3.91x |
| 64 | omp_tiled vs tiled | 1.70x | 1.70x to 1.70x |
| 64 | omp_tiled vs omp_scalar | 0.99x | 0.98x to 0.99x |
| 64 | omp_tiled vs omp_heap | 1.05x | 1.05x to 1.05x |
| 64 | omp_tiled vs scalar | 2.79x | 2.79x to 2.79x |
| 64 | omp_tiled vs heap | 6.58x | 6.57x to 6.63x |
| 1024 | tiled vs scalar | 0.37x | 0.37x to 0.37x |
| 1024 | tiled vs heap | 0.86x | 0.86x to 0.86x |
| 1024 | omp_tiled vs tiled | 1.48x | 1.48x to 1.49x |
| 1024 | omp_tiled vs omp_scalar | 0.23x | 0.23x to 0.23x |
| 1024 | omp_tiled vs omp_heap | 0.22x | 0.22x to 0.23x |
| 1024 | omp_tiled vs scalar | 0.55x | 0.55x to 0.55x |
| 1024 | omp_tiled vs heap | 1.27x | 1.27x to 1.28x |

## Same configuration in different sweeps

21 configurations appear in more than one sweep. Largest gaps:

| Configuration | Milliseconds | Gap |
|---|---|---|
| omp_tiled N=8388608 K=64 P=8 | size 0.810, k 0.814, dist 0.903 | 11.5% |
| tiled N=8388608 K=8 P=1 | size 1.354, k 1.335 | 1.4% |
| omp_tiled N=8388608 K=1024 P=8 | size 14.063, k 14.191 | 0.9% |
| tiled N=8388608 K=64 P=1 | size 1.438, k 1.429, tile 1.428, dist 1.439 | 0.8% |
| scalar N=8388608 K=64 P=1 | size 2.269, k 2.273, dist 2.256 | 0.8% |
| heap N=8388608 K=8 P=1 | size 5.569, k 5.528 | 0.7% |
| tiled N=8388608 K=16 P=1 | k 1.344, tile 1.335 | 0.6% |
| scalar N=8388608 K=8 P=1 | size 2.253, k 2.262 | 0.4% |
| nth N=8388608 K=8 P=1 | size 59.924, k 60.145 | 0.4% |
| omp_tiled N=33554432 K=1024 P=8 | size 18.491, scaling 18.425 | 0.4% |
