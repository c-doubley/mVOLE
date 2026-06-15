# ModuleMVOLE Prototype Benchmarks

## Scope

These are lightweight local benchmarks for the current ModuleMVOLE algebraic correctness harness. They do not include PPRF generation, silent setup, communication, malicious checks, or final protocol integration.

The benchmark mode times:

- sparse/base Gen: public `rho`, sparse `s/e`, `rho*s + e`, and precomputed `WHT(rho)`
- direct sharing masks: local random row masks for `Delta*s` and `Delta*e`
- scalar WHT for `x`
- row-wise WHT work for `Z0`
- row-wise WHT work for `Z1`
- verification: entrywise check of `Z0[h][j] + Z1[h][j] == Delta[h] * x[j]`

The benchmark also reports totals both with and without verification.

## Build

```bash
cmake --build build --parallel 64
```

## Commands

```bash
./build/main --MODULE_MVOLE_BENCH 1
./build/main --MODULE_MVOLE_BENCH 2
./build/main --MODULE_MVOLE_BENCH 3
```

Correctness presets were also rerun:

```bash
./build/main --MODULE_MVOLE 1
./build/main --MODULE_MVOLE 2
./build/main --MODULE_MVOLE 3
```

## Local optimization applied

The earlier Phase 3C benchmark used direct dense XOR-convolution during every P0/P1 row expansion and then immediately applied WHT to the result. Phase 3D replaces that local computation with the equivalent WHT-domain identity:

```text
WHT(rho * row + mask) = WHT(rho) * WHT(row) + WHT(mask)
```

This is not a PPRF/silent setup integration and does not change the ModuleMVOLE correctness relation. It only avoids repeated dense `O(N^2)` row convolutions in the local harness. The base `rho*s` step now also uses a sparse-RHS convolution because `s` is sparse.

## Detailed Results

Fp64 benchmark output:

| Command | `N` | `t` | `m` | Output elems | Gen base s | Direct masks s | Gen total s | Expand P0 s | Expand P1 s | `x` WHT s | `Z0` WHT_m s | `Z1` WHT_m s | Verify s | Total no verify s | Total with verify s | Throughput no verify elems/s | Throughput with verify elems/s | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `./build/main --MODULE_MVOLE_BENCH 1` | 1024 | 8 | 8 | 8192 | 0.000072 | 0.000292 | 0.000364 | 0.000205 | 0.000207 | 0.000008 | 0.000121 | 0.000121 | 0.000070 | 0.000776 | 0.000846 | 10552846.481711 | 9682038.250260 | PASS |
| `./build/main --MODULE_MVOLE_BENCH 2` | 4096 | 16 | 16 | 65536 | 0.000383 | 0.002322 | 0.002704 | 0.001668 | 0.001620 | 0.000032 | 0.000990 | 0.000992 | 0.000285 | 0.005993 | 0.006278 | 10935259.401610 | 10439456.624918 | PASS |
| `./build/main --MODULE_MVOLE_BENCH 3` | 16384 | 32 | 16 | 262144 | 0.002335 | 0.008831 | 0.011166 | 0.007136 | 0.007001 | 0.000140 | 0.004355 | 0.004411 | 0.001144 | 0.025304 | 0.026448 | 10359886.864787 | 9911717.540682 | PASS |

## WHT Microbenchmarks

The benchmark mode also runs small WHT-only probes on the same field context:

| Command | `N` | `m` | One-vector WHT s | WHT_m s |
| --- | ---: | ---: | ---: | ---: |
| `./build/main --MODULE_MVOLE_BENCH 1` | 1024 | 8 | 0.000008 | 0.000105 |
| `./build/main --MODULE_MVOLE_BENCH 2` | 4096 | 16 | 0.000033 | 0.000849 |
| `./build/main --MODULE_MVOLE_BENCH 3` | 16384 | 16 | 0.000141 | 0.003612 |

## Correctness Check

Output from the correctness presets after the optimization:

```text
MODULE_MVOLE Fp64 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp32 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp64 N=1024 t=8 m=8 PASS
MODULE_MVOLE Fp32 N=1024 t=8 m=8 PASS
MODULE_MVOLE Fp64 N=2048 t=16 m=8 PASS
MODULE_MVOLE Fp32 N=2048 t=16 m=8 PASS
```

## Bottleneck Diagnosis

Before Phase 3D, preset `3` (`N = 2^14`, `t = 32`, `m = 16`) took about 29 seconds because each row expansion performed dense XOR-convolution before WHT. That dense convolution dominated the cost.

After the local WHT-domain optimization, preset `3` completes in about 0.026 seconds including verification. The remaining costs are mainly:

- direct random mask generation, about 0.0088 seconds
- row-wise WHT and output expansion for `Z0/Z1`, about 0.0141 seconds combined
- verification, about 0.0011 seconds

This benchmark is still only for the algebraic local harness. The next meaningful bottleneck will come from the real PPRF/silent setup once that path is integrated.

## Phase 3E Scaling Grid

The benchmark preset map was extended for the requested optimized local-algebra/WHT scaling runs:

| Preset | `N` | `t` | `m` |
| ---: | ---: | ---: | ---: |
| 5 | 16384 | 32 | 8 |
| 3 | 16384 | 32 | 16 |
| 6 | 16384 | 32 | 32 |
| 7 | 16384 | 32 | 64 |
| 8 | 65536 | 64 | 8 |
| 9 | 65536 | 64 | 16 |
| 4 | 65536 | 64 | 32 |
| 10 | 262144 | 64 | 8 |
| 11 | 262144 | 64 | 16 |

Commands:

```bash
./build/main --MODULE_MVOLE_BENCH 5
./build/main --MODULE_MVOLE_BENCH 3
./build/main --MODULE_MVOLE_BENCH 6
./build/main --MODULE_MVOLE_BENCH 7
./build/main --MODULE_MVOLE_BENCH 8
./build/main --MODULE_MVOLE_BENCH 9
./build/main --MODULE_MVOLE_BENCH 4
./build/main --MODULE_MVOLE_BENCH 10
./build/main --MODULE_MVOLE_BENCH 11
```

All requested grid points completed under the 60-second cap.

| `N` | `t` | `m` | Output elems `m*N` | Gen s | Expand P0 s | Expand P1 s | Verify s | Total no verify s | Total with verify s | Throughput no verify elems/s | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 16384 | 32 | 8 | 131072 | 0.007069 | 0.003673 | 0.003558 | 0.000575 | 0.014300 | 0.014874 | 9166062.815635 | PASS |
| 16384 | 32 | 16 | 262144 | 0.011065 | 0.007141 | 0.006996 | 0.001149 | 0.025202 | 0.026352 | 10401622.454079 | PASS |
| 16384 | 32 | 32 | 524288 | 0.019344 | 0.014077 | 0.013913 | 0.002288 | 0.047334 | 0.049623 | 11076257.653062 | PASS |
| 16384 | 32 | 64 | 1048576 | 0.037429 | 0.027976 | 0.028138 | 0.004552 | 0.093543 | 0.098095 | 11209550.155634 | PASS |
| 65536 | 64 | 8 | 524288 | 0.034613 | 0.015325 | 0.014843 | 0.002282 | 0.064780 | 0.067063 | 8093330.890952 | PASS |
| 65536 | 64 | 16 | 1048576 | 0.051733 | 0.029698 | 0.029309 | 0.004559 | 0.110740 | 0.115298 | 9468826.196494 | PASS |
| 65536 | 64 | 32 | 2097152 | 0.088302 | 0.059376 | 0.058649 | 0.009126 | 0.206327 | 0.215452 | 10164233.102028 | PASS |
| 262144 | 64 | 8 | 2097152 | 0.140775 | 0.075154 | 0.072352 | 0.009158 | 0.288281 | 0.297440 | 7274673.773547 | PASS |
| 262144 | 64 | 16 | 4194304 | 0.209202 | 0.145164 | 0.142434 | 0.018240 | 0.496800 | 0.515040 | 8442639.353315 | PASS |

The optimized local layer sustains roughly 7.3M to 11.2M output field elements per second without verification over this grid. Gen and row expansion scale approximately linearly in `m*N`; verification remains a small fraction of total time.

## Original QA-SD VOLE Comparison

Closest available original benchmark/test entry point:

```bash
./build/main --QA_VOLE n
```

This passes `N = 2^n` to `VOLE_prime_QASD` and runs both field contexts, Fp64 first and Fp32 second.

Observed output:

```text
COMMAND ./build/main --QA_VOLE 14
The total time of VOLE based on QA-SD code consume 0.0162073 seconds
The total time of VOLE based on QA-SD code consume 0.0149026 seconds
COMMAND ./build/main --QA_VOLE 16
The total time of VOLE based on QA-SD code consume 0.0235897 seconds
The total time of VOLE based on QA-SD code consume 0.0194725 seconds
COMMAND ./build/main --QA_VOLE 18
The total time of VOLE based on QA-SD code consume 0.0575277 seconds
The total time of VOLE based on QA-SD code consume 0.0428349 seconds
```

Comparison notes:

- `QA_VOLE` is the original scalar QA-SD VOLE wrapper for vectors of length `N`.
- `MODULE_MVOLE_BENCH` measures only the optimized local algebra/WHT layer for matrix-shaped output of size `m*N`.
- The current ModuleMVOLE benchmark does not include PPRF/silent setup, communication, or malicious checks.
- Therefore the comparison is closest by `N`, but not exact by functionality or output size. The meaningful Phase 3E takeaway is that the local ModuleMVOLE algebra/WHT layer is no longer the bottleneck at these tested sizes.
