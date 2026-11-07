# Task 2 assembly audit

Toolchain: `Apple clang version 21.0.0 (clang-2100.1.1.101), flags: -std=c++17 -O3 -mavx2 -S -fno-asynchronous-unwind-tables -fno-exceptions -arch x86_64`

Regenerate with `python3 inspect_asm.py`. Spills count ymm stores to the stack.

| Stage | Instructions | Vector | Conditional jumps | ymm spills |
|---|---|---|---|---|
| `probe_normalize_nan` | 11 | 7 | 0 | 0 |
| `probe_sort_block` | 32 | 28 | 0 | 0 |
| `probe_merge_pair` | 62 | 58 | 0 | 0 |
| `probe_merge_k8` | 65 | 61 | 0 | 0 |
| `probe_merge_k64` | 95 | 87 | 1 | 0 |
| `probe_merge_k1024` | 95 | 87 | 1 | 0 |
| `simd_bitonic_merge_8` | 373 | 274 | 57 | 0 |

## Verdict

- Bitonic network and NaN handling have zero conditional jumps: yes
- Reservoir merge for K % 8 == 0 has only the chunk loop's back-edge, which depends on K and never on the data: yes
- No ymm register spills on the data path: yes
- `simd_bitonic_merge_8` itself still holds 57 conditional jumps and 0 ymm stack stores. They belong to the K dispatch, the chunk loop and the scalar path that only runs when K % 8 != 0.

## Instruction mix of `probe_sort_block`

- `vblendps`: 6
- `vmaxps`: 6
- `vminps`: 6
- `vshufps`: 3
- `vmovups`: 2
- `vshufpd`: 2
- `movq`: 1
- `popq`: 1
- `pushq`: 1
- `retq`: 1
- `vextractf128`: 1
- `vpermpd`: 1
- `vzeroupper`: 1
