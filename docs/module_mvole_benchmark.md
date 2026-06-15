# ModuleMVOLE Prototype Benchmarks

## Scope

These are lightweight local benchmarks for the current ModuleMVOLE algebraic correctness harness. They do not include PPRF generation, silent setup, communication, malicious checks, or final protocol integration.

The benchmark mode times:

- local Gen: base ring state, `Delta`, and direct row-mask generation
- Expand P0: P0 output expansion, including module ring multiplication and WHT work
- Expand P1: P1 output expansion, including module ring multiplication and WHT work
- verification: entrywise check of `Z0[h][j] + Z1[h][j] == Delta[h] * x[j]`

The current prototype uses direct `O(N^2)` XOR-convolution ring multiplication, so these timings should not be interpreted as final protocol performance.

## Build

```bash
cmake --build build --parallel 64
```

## Commands

```bash
./build/main --MODULE_MVOLE_BENCH 1
./build/main --MODULE_MVOLE_BENCH 2
./build/main --MODULE_MVOLE_BENCH 3
timeout 30s ./build/main --MODULE_MVOLE_BENCH 4
```

## Results

Fp64 was benchmarked first. Fp32 benchmark reporting was not added in this phase because the Fp64 direct-convolution cost already reaches the lightweight limit at `N = 2^14`.

| Command | `N` | `t` | `m` | Output elems `m*N` | Gen s | Expand P0 s | Expand P1 s | Verify s | Total s | Throughput elems/s | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `./build/main --MODULE_MVOLE_BENCH 1` | 1024 | 8 | 8 | 8192 | 0.003778 | 0.027751 | 0.027681 | 0.000042 | 0.059253 | 138255.594706 | PASS |
| `./build/main --MODULE_MVOLE_BENCH 2` | 4096 | 16 | 16 | 65536 | 0.057450 | 0.880187 | 0.879864 | 0.000293 | 1.817793 | 36052.501692 | PASS |
| `./build/main --MODULE_MVOLE_BENCH 3` | 16384 | 32 | 16 | 262144 | 0.887434 | 14.054739 | 14.061557 | 0.004700 | 29.008430 | 9036.821246 | PASS |

Preset `4` maps to `N = 65536`, `t = 64`, `m = 32`. It did not complete within a 30-second cap:

```bash
timeout 30s ./build/main --MODULE_MVOLE_BENCH 4
```

Exit code:

```text
124
```

The largest successful lightweight benchmark in this phase is therefore preset `3`, `N = 2^14`, `t = 32`, `m = 16`.

## Correctness Check

After adding the benchmark flag, the original Phase 3A/3B correctness command was rerun:

```bash
./build/main --MODULE_MVOLE 1
```

Output:

```text
MODULE_MVOLE Fp64 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp32 N=256 t=4 m=4 PASS
```
