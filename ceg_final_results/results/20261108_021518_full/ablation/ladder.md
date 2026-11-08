# Optimisation ladder

N = 33554432, uniform input. Each value is the median of 3 rounds, and each round is that configuration's median over its repetitions. 72 rows, 0 failed verification.

## K = 64

| Step | P | Median ms | Range ms | GB/s | vs previous | vs min-heap | vs scalar |
|---|---|---|---|---|---|---|---|
| Min-heap | 1 | 21.839 | 21.766 to 21.842 | 6.1 | - | 1.00x | 0.43x |
| Hand-written scalar | 1 | 9.306 | 9.223 to 9.351 | 14.4 | 2.35x | 2.35x | 1.00x |
| Tiled, no filter | 1 | 613.7 | 608.5 to 614.0 | 0.2 | 0.02x | 0.04x | 0.02x |
| Tiled, scalar filter | 1 | 9.283 | 9.281 to 9.296 | 14.5 | 66.11x | 2.35x | 1.00x |
| Tiled, vector filter | 1 | 5.779 | 5.775 to 5.795 | 23.2 | 1.61x | 3.78x | 1.61x |
| OpenMP tiled, P = 2 | 2 | 3.747 | 3.746 to 3.758 | 35.8 | 1.54x | 5.83x | 2.48x |
| OpenMP tiled, P = 4 | 4 | 3.427 | 3.419 to 3.429 | 39.2 | 1.09x | 6.37x | 2.72x |
| OpenMP tiled, P = 8 | 8 | 5.346 | 4.695 to 5.436 | 25.1 | 0.64x | 4.09x | 1.74x |

Filter against merge, one thread:

| Filter | Scalar merge (K = 63), ms | Vector merge (K = 64), ms | Vector merge speedup |
|---|---|---|---|
| None | 109.7 | 613.7 | 0.18x |
| Scalar | 9.226 | 9.283 | 0.99x |
| Vector | 5.793 | 5.779 | 1.00x |

The scalar merge skips every value at or below the current bound by itself, so the "None" row is still filtered value by value in its scalar column. Compare the two merges only on the Scalar and Vector rows.

Tiling off (B = N): 5.482 ms against 5.779 ms at B = 1024.

## K = 1024

| Step | P | Median ms | Range ms | GB/s | vs previous | vs min-heap | vs scalar |
|---|---|---|---|---|---|---|---|
| Min-heap | 1 | 22.632 | 22.564 to 22.708 | 5.9 | - | 1.00x | 0.44x |
| Hand-written scalar | 1 | 9.903 | 9.861 to 9.944 | 13.6 | 2.29x | 2.29x | 1.00x |
| Tiled, no filter | 1 | 9627.0 | 9588.6 to 9638.3 | 0.0 | 0.00x | 0.00x | 0.00x |
| Tiled, scalar filter | 1 | 29.979 | 29.886 to 30.085 | 4.5 | 321.12x | 0.75x | 0.33x |
| Tiled, vector filter | 1 | 26.374 | 26.373 to 26.410 | 5.1 | 1.14x | 0.86x | 0.38x |
| OpenMP tiled, P = 2 | 2 | 22.651 | 22.560 to 22.735 | 5.9 | 1.16x | 1.00x | 0.44x |
| OpenMP tiled, P = 4 | 4 | 20.523 | 20.500 to 20.543 | 6.5 | 1.10x | 1.10x | 0.48x |
| OpenMP tiled, P = 8 | 8 | 18.450 | 18.404 to 18.454 | 7.3 | 1.11x | 1.23x | 0.54x |

Filter against merge, one thread:

| Filter | Scalar merge (K = 1023), ms | Vector merge (K = 1024), ms | Vector merge speedup |
|---|---|---|---|
| None | 124.2 | 9627.0 | 0.01x |
| Scalar | 22.898 | 29.979 | 0.76x |
| Vector | 19.274 | 26.374 | 0.73x |

The scalar merge skips every value at or below the current bound by itself, so the "None" row is still filtered value by value in its scalar column. Compare the two merges only on the Scalar and Vector rows.

Tiling off (B = N): 26.045 ms against 26.374 ms at B = 1024.

