# ModuleMVOLE Prototype

## Files changed

| File | Purpose |
| --- | --- |
| `ModuleMVOLE.h` | Header-only minimal ModuleMVOLE algebraic correctness harness. Defines the parameters, P0/P1 output containers, local Gen/Expand helpers, verifier, and `ModuleMVOLE_Test`. |
| `main.cpp` | Adds `#include "ModuleMVOLE.h"`, a usage line, and the `--MODULE_MVOLE` command-line entry point. |
| `docs/module_mvole_prototype.md` | Documents the prototype, presets, and reproduction commands. |

## Build command

```bash
cmake --build build --parallel 64
```

## Run commands

```bash
./build/main --MODULE_MVOLE 1
./build/main --MODULE_MVOLE 2
./build/main --MODULE_MVOLE 3
```

Observed output:

```text
MODULE_MVOLE Fp64 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp32 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp64 N=1024 t=8 m=8 PASS
MODULE_MVOLE Fp32 N=1024 t=8 m=8 PASS
MODULE_MVOLE Fp64 N=2048 t=16 m=8 PASS
MODULE_MVOLE Fp32 N=2048 t=16 m=8 PASS
```

## Tested parameters

The smoke-test presets map to:

| Command | `n` | `N` | `t` | `m` | Fields |
| --- | --- | --- | --- | --- | --- |
| `./build/main --MODULE_MVOLE 1` | 8 | 256 | 4 | 4 | existing 64-bit and 32-bit prime-field contexts |
| `./build/main --MODULE_MVOLE 2` | 10 | 1024 | 8 | 8 | existing 64-bit and 32-bit prime-field contexts |
| `./build/main --MODULE_MVOLE 3` | 11 | 2048 | 16 | 8 | existing 64-bit and 32-bit prime-field contexts |

Preset `1` preserves the Phase 3A behavior exactly.

## Internal structure

The Phase 3B refactor keeps the same direct algebra but separates the harness into protocol-style local helpers:

| Helper | Role |
| --- | --- |
| `moduleMvoleGenBase` | Samples local `rho`, sparse `s`, sparse `e`, and computes `b = rho * s + e` in the XOR-convolution ring. |
| `moduleMvoleGenDelta` | Samples the receiver-side vector `Delta in F_p^m`. |
| `moduleMvoleGenRowMasks` | Creates direct random row masks for the algebraic shares of `Delta * s` and `Delta * e`. |
| `moduleMvoleGenDirect` | Combines the local Gen steps for this correctness harness. |
| `moduleMvoleExpandP0` | Expands P0 output `x` and `Z0`. |
| `moduleMvoleExpandP1` | Expands P1 output `Delta` and `Z1`. |
| `moduleMvoleVerify` | Checks the matrix VOLE relation entry by entry. |

## Correctness relation verified

The verifier checks every row `h` and column `j` of the matrix output:

```text
Z0[h][j] + Z1[h][j] == Delta[h] * x[j]
```

Here P0 outputs `x in F_p^N` and `Z0 in F_p^{m x N}`, while P1 outputs `Delta in F_p^m` and `Z1 in F_p^{m x N}`.

## Prototype simplifications

This prototype is an algebraic correctness harness. It uses the existing prime-field arithmetic, matrix container, XOR-convolution ring multiplication, and WHT routine, but it does not yet implement the final PPRF/silent setup path.

Main simplifications:

- `Delta` is represented as `m` independent ordinary `F_p` elements, not as an element of `F_{p^m}`.
- The construction uses direct random masks for the row-wise shares of `Delta * s` and `Delta * e`.
- Sparse `s` and `e` are sampled locally in the harness.
- No malicious security is implemented.
- No network protocol, PPRF correlation generation, or silent VOLE setup is performed.
- Ring multiplication is direct `O(N^2)` XOR convolution, so the presets are intentionally small smoke tests rather than benchmarks.

## Recommended Phase 3C next steps

1. Replace the direct random masks with a vector-valued PPRF or domain-separated wrapper around the existing PPRF path.
2. Connect the prototype to the existing QA-SD VOLE setup/expand structure instead of standalone local sampling.
3. Preserve the current verifier as a small regression test while adding protocol-level tests.
4. Add parameter handling for larger `N`, `t`, and `m` once the PPRF-backed path is wired in.
