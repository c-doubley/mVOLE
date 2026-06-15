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
