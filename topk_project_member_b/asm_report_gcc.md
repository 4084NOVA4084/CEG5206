# Task 2 assembly audit

Toolchain: `g++ (Ubuntu 9.4.0-1ubuntu1~20.04.2) 9.4.0, flags: -std=c++17 -O3 -mavx2 -S -fno-asynchronous-unwind-tables -fno-exceptions`

Regenerate with `python3 inspect_asm.py`. Spills count ymm stores to the stack.

| Stage | Instructions | Vector | Conditional jumps | ymm spills |
|---|---|---|---|---|
| `probe_normalize_nan` | 9 | 7 | 0 | 0 |
| `probe_sort_block` | 40 | 38 | 0 | 0 |
| `probe_merge_pair` | 72 | 70 | 0 | 0 |
| `probe_merge_k8` | 71 | 69 | 0 | 0 |
| `probe_merge_k64` | 101 | 94 | 1 | 0 |
| `probe_merge_k1024` | 101 | 94 | 1 | 0 |
| `simd_bitonic_merge_8` | 190 | 148 | 11 | 1 |

## Verdict

- Bitonic network and NaN handling have zero conditional jumps: yes
- Reservoir merge for K % 8 == 0 has only the chunk loop's back-edge, which depends on K and never on the data: yes
- No ymm register spills anywhere: NO
- `simd_bitonic_merge_8` itself still holds 11 conditional jumps. They belong to the K dispatch, the chunk loop and the scalar path that only runs when K % 8 != 0.

## Instruction mix of `probe_sort_block`

- `vblendvps`: 6
- `vmaxps`: 6
- `vminps`: 6
- `vmovaps`: 6
- `vpermps`: 6
- `vmovdqa`: 3
- `vmovups`: 2
- `endbr64`: 1
- `ret`: 1
- `vextractf128`: 1
- `vinsertf128`: 1
- `vzeroupper`: 1

## Note

Copied from the Ryzen 7 5800H run. It was generated before the verdict was narrowed to the data path, which is why it still says NO for spills: the one ymm stack store is in `simd_bitonic_merge_8`, and every data-path stage shows 0.
