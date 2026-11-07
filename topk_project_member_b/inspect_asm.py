#!/usr/bin/env python3
"""Task 2 evidence: compile the kernel for x86-64 + AVX2 and audit the assembly.

    python3 inspect_asm.py              # writes asm_report.md and build/asm_probe.s

For each stage in asm_probe.cpp it counts conditional jumps (data-dependent or
not) and ymm values spilled to the stack. Compiler Explorer shows the same
output for the same compiler and flags; this makes the check repeatable.

Runs on x86-64 directly. On Apple Silicon it cross-compiles with `-arch x86_64`,
which needs no x86 CPU because the code is only compiled, never executed.
"""
from __future__ import annotations

import collections
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE / "build"
SOURCE = HERE / "asm_probe.cpp"
OUTPUT = BUILD / "asm_probe.s"
REPORT = HERE / "asm_report.md"

FLAGS = ["-std=c++17", "-O3", "-mavx2", "-S", "-fno-asynchronous-unwind-tables",
         "-fno-exceptions"]
# Local labels start with L or .L, so only function names match here. GCC's split
# and cloned functions carry a dot (foo.cold, foo.constprop.0).
FUNCTION_LABEL = re.compile(r"^(?!L)([A-Za-z_][\w$.]*):")
COND_JUMP = re.compile(r"^j(?!mp\b)[a-z]+$")
YMM_STORE_TO_STACK = re.compile(r"%ymm\d+,\s*-?\w*\(%r[sb]p")

PUBLIC_KERNEL = "simd_bitonic_merge_8"
# The network itself: no conditional jump at all.
BRANCH_FREE = ["probe_normalize_nan", "probe_sort_block", "probe_merge_pair", "probe_merge_k8"]
# Whole reservoir merge: at most the chunk loop's back-edge, which depends on K alone.
LOOP_ONLY = ["probe_merge_k64", "probe_merge_k1024"]


def compile_probe() -> str:
    compiler = os.environ.get("CXX", "clang++" if sys.platform == "darwin" else "g++")
    cmd = [compiler, *FLAGS]
    if platform.machine() not in ("x86_64", "AMD64"):
        if sys.platform != "darwin":
            sys.exit("needs an x86-64 host, or macOS for -arch x86_64")
        cmd += ["-arch", "x86_64"]
    BUILD.mkdir(exist_ok=True)
    cmd += [str(SOURCE.name), "-o", str(OUTPUT)]
    subprocess.run(cmd, check=True, cwd=HERE)
    version = subprocess.run([compiler, "--version"], capture_output=True, text=True)
    return f"{version.stdout.splitlines()[0]}, flags: {' '.join(cmd[1:-3])}"


def split_functions(text: str) -> dict:
    functions: dict = {}
    current = None
    for raw in text.splitlines():
        line = raw.split("#")[0].strip()
        label = FUNCTION_LABEL.match(line)
        if label:
            # foo.cold is the rest of foo, so its jumps count towards foo.
            name = label.group(1).lstrip("_").split(".")[0]
            if name.startswith("probe_"):
                current = name
            elif PUBLIC_KERNEL in name:
                current = PUBLIC_KERNEL
            else:
                current = None
            if current:
                functions.setdefault(current, [])
        elif current and line and not line.startswith(".") and not line.endswith(":"):
            functions[current].append(line)
    return functions


def audit(lines: list) -> dict:
    jumps = []
    spills = 0
    mnemonics: collections.Counter = collections.Counter()
    for line in lines:
        fields = line.split(None, 1)  # compilers separate mnemonic and operands with a tab
        op = fields[0]
        operands = fields[1] if len(fields) > 1 else ""
        mnemonics[op] += 1
        if COND_JUMP.match(op):
            jumps.append(line)
        # AT&T order is source, destination: a store has memory last.
        if op.startswith("vmov") and YMM_STORE_TO_STACK.search(operands):
            spills += 1
    vector = sum(n for op, n in mnemonics.items() if op.startswith("v"))
    return {"instructions": sum(mnemonics.values()), "vector": vector, "jumps": jumps,
            "spills": spills, "mnemonics": mnemonics}


def main() -> int:
    toolchain = compile_probe()
    results = {name: audit(lines) for name, lines in split_functions(OUTPUT.read_text()).items()}
    # GCC can inline the public function into its one caller, leaving no symbol of its own.
    if PUBLIC_KERNEL not in results and "probe_merge_any_k" in results:
        results[PUBLIC_KERNEL] = results["probe_merge_any_k"]
    order = BRANCH_FREE + LOOP_ONLY + [PUBLIC_KERNEL]
    missing = [name for name in order if name not in results]
    if missing:
        print(f"not found in {OUTPUT}: {missing}", file=sys.stderr)
        return 1

    out = ["# Task 2 assembly audit", "",
           f"Toolchain: `{toolchain}`", "",
           "Regenerate with `python3 inspect_asm.py`. Spills count ymm stores to the stack.", "",
           "| Stage | Instructions | Vector | Conditional jumps | ymm spills |",
           "|---|---|---|---|---|"]
    for name in order:
        r = results[name]
        out.append(f"| `{name}` | {r['instructions']} | {r['vector']} "
                   f"| {len(r['jumps'])} | {r['spills']} |")

    branch_free = all(not results[n]["jumps"] for n in BRANCH_FREE)
    loop_only = all(len(results[n]["jumps"]) <= 1 for n in LOOP_ONLY)
    no_spills = all(results[n]["spills"] == 0 for n in BRANCH_FREE + LOOP_ONLY)
    out += ["", "## Verdict", "",
            f"- Bitonic network and NaN handling have zero conditional jumps: "
            f"{'yes' if branch_free else 'NO'}",
            f"- Reservoir merge for K % 8 == 0 has only the chunk loop's back-edge, which "
            f"depends on K and never on the data: {'yes' if loop_only else 'NO'}",
            f"- No ymm register spills on the data path: {'yes' if no_spills else 'NO'}",
            f"- `{PUBLIC_KERNEL}` itself still holds "
            f"{len(results[PUBLIC_KERNEL]['jumps'])} conditional jumps and "
            f"{results[PUBLIC_KERNEL]['spills']} ymm stack stores. They belong to the K dispatch, "
            "the chunk loop and the scalar path that only runs when K % 8 != 0.",
            "", "## Instruction mix of `probe_sort_block`", ""]
    mix = results["probe_sort_block"]["mnemonics"]
    for op, n in sorted(mix.items(), key=lambda kv: (-kv[1], kv[0])):
        out.append(f"- `{op}`: {n}")

    REPORT.write_text("\n".join(out) + "\n")
    print("\n".join(out))
    return 0 if branch_free and loop_only and no_spills else 1


if __name__ == "__main__":
    sys.exit(main())
