# Phase 4E Vector PPRF Optimization Note

## Goal

Phase 4E optimizes the local shared-path vector-valued PPRF setup used by:

```bash
./build/main --MODULE_MVOLE_PPRF <preset>
```

The correctness relation is unchanged:

```text
Z0 + Z1 = Delta * x in R_{p^m}
```

This remains a local semi-honest prototype. It does not modify `QA_VOLE`, `SilentWalshVoleSender`, or `SilentWalshVoleReceiver`.

## Implementation Change

## Phase 4F Pre-Change Finding

Before Phase 4F, `--MODULE_MVOLE_PPRF` still expanded one full-domain vector PPRF tree per sparse point. The relevant code path was:

```text
moduleMvoleBuildVectorPprfPoints
moduleMvoleExpandVectorPprfIntoRows
moduleVectorPprfExpandSharedTree(point.rootSeed, params.N, leaves)
```

Since there are `t` nonzero sparse points and each point expanded over domain `N`, each sparse vector expanded:

```text
expanded leaves = t * N
```

The intended regular-noise structure is one point per block, with:

```text
blockSize = N / t
alpha_i = i * blockSize + offset_i
```

That should expand one tree of size `blockSize` per block, so each sparse vector expands:

```text
expanded leaves = t * blockSize = N
```

Therefore the pre-Phase-4F implementation was `O(tN)` in the PPRF expansion for each sparse vector, not `O(N)`.

The previous Phase 4C integration generated full temporary matrices:

```text
v0, v1 for Delta*s
u0, u1 for Delta*e
```

and then copied those matrices into the RM-VOLE row-share structure.

Phase 4E adds a direct aggregation path in `ModuleMvolePprfIntegration.h`:

```text
moduleMvoleInitPprfRows
moduleMvoleAddToVec
moduleMvoleExpandVectorPprfIntoRows
```

The vector-PPRF point functions are now accumulated directly into:

```text
rowMasks[h].V,  rowMasks[h].W0   for Delta*s
rowMasks[h].U,  rowMasks[h].W1   for Delta*e
```

This avoids allocating and copying the intermediate `m x N` matrices in the RM-VOLE_PPRF path. The code is still intentionally marked and structured as a local prototype.

`ModuleVectorPprfTest.h` also uses an in-place shared-tree leaf buffer. This keeps one leaf frontier buffer per point-function expansion and preserves the shared-path structure across all `m` coordinates.

## Verification Commands

```bash
cmake --build build --parallel 64
./build/main --MODULE_MVOLE_PPRF 1
./build/main --MODULE_MVOLE_PPRF 2
./build/main --MODULE_MVOLE_PPRF 3
./build/main --MODULE_VECTOR_PPRF_TEST 1
./build/main --MODULE_MVOLE 1
./build/main --QA_VOLE 10
./build/main --QA_VOLE 12
./build/main --QA_VOLE 14
```

All commands passed.

## Before and After

The before numbers are the Phase 4D RM-VOLE_PPRF measurements. The after numbers are the final Phase 4E measurements.

| Preset | `N` | `t` | `m` | Before total s | After total s | Improvement | Speedup |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1024 | 8 | 8 | 0.002339 | 0.002138 | 8.59% | 1.09x |
| 2 | 4096 | 16 | 16 | 0.030242 | 0.027254 | 9.88% | 1.11x |
| 3 | 16384 | 32 | 16 | 0.218113 | 0.204698 | 6.15% | 1.07x |

## Phase 4F Regular-Block PPRF

Phase 4F changes `--MODULE_MVOLE_PPRF` to use regular sparse noise. Each sparse vector has one nonzero coefficient per block:

```text
blockSize = N / t
alpha_i = i * blockSize + offset_i
```

The vector PPRF now expands over `blockSize` for each block and writes directly into global position `i * blockSize + j`. This changes the local PPRF leaf count per sparse vector from:

```text
before: t * N
after:  t * (N/t) = N
```

| Preset | `N` | `t` | Block size | Before leaves / sparse vector | After leaves / sparse vector | After leaves / `N` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1024 | 8 | 128 | 8192 | 1024 | 1.000000 |
| 2 | 4096 | 16 | 256 | 65536 | 4096 | 1.000000 |
| 3 | 16384 | 32 | 512 | 524288 | 16384 | 1.000000 |

## Phase 4F Before and After

The before numbers are the final Phase 4E measurements. The after numbers are the regular-block Phase 4F measurements.

| Preset | `N` | `t` | `m` | Phase 4E total s | Phase 4F total s | Improvement | Speedup |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1024 | 8 | 8 | 0.002138 | 0.000891 | 58.33% | 2.40x |
| 2 | 4096 | 16 | 16 | 0.027254 | 0.006740 | 75.27% | 4.04x |
| 3 | 16384 | 32 | 16 | 0.204698 | 0.028852 | 85.91% | 7.09x |

## Final RM-VOLE_PPRF Timing

| Preset | `N` | `t` | `m` | Block size | Expanded leaves / sparse vector | PPRF `s` setup s | PPRF `e` setup s | Expand P0 s | Expand P1 s | Verify s | Total s | Throughput elems/s | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 1024 | 8 | 8 | 128 | 1024 | 0.000094 | 0.000090 | 0.000248 | 0.000197 | 0.000048 | 0.000891 | 9197162.902040 | PASS |
| 2 | 4096 | 16 | 16 | 256 | 4096 | 0.000735 | 0.000716 | 0.001771 | 0.001656 | 0.000293 | 0.006740 | 9723339.883520 | PASS |
| 3 | 16384 | 32 | 16 | 512 | 16384 | 0.003116 | 0.003233 | 0.007516 | 0.007180 | 0.001139 | 0.028852 | 9085947.232089 | PASS |

## Comparison Against `m * QA_VOLE`

The QA-SD VOLE comparison is still only a cost proxy. RM-VOLE_PPRF produces one shared right vector `x` and one extension-field scalar `Delta`, not `m` independent VOLE instances with independent right vectors.

| `N` | `m` | RM-VOLE_PPRF total s | QA_VOLE Fp64 s | Estimated `m * QA_VOLE` s | Speedup vs estimate |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1024 | 8 | 0.000891 | 0.0145924 | 0.1167392 | 131.02x |
| 4096 | 16 | 0.006740 | 0.0148057 | 0.2368912 | 35.15x |
| 16384 | 16 | 0.028852 | 0.0165377 | 0.2646032 | 9.17x |

The Phase 4F regular-block optimization improves the comparison at all three implemented RM-VOLE_PPRF presets. The comparison is still only a cost proxy for workloads that naturally need the shared-right-vector rank-1 RM-VOLE correlation.

## Remaining Bottleneck

The local vector-PPRF setup is no longer the dominant cost. After Phase 4F, the largest named cost is the RM-VOLE P0/P1 expansion, mainly row-wise WHT work:

| `N` | `m` | PPRF setup share of total | P0+P1 expansion share |
| ---: | ---: | ---: | ---: |
| 1024 | 8 | 20.65% | 49.94% |
| 4096 | 16 | 21.53% | 50.85% |
| 16384 | 16 | 22.01% | 50.94% |

The next optimization target is to stream or fuse the row-share representation into WHT/evaluation-friendly buffers. The eventual protocol path should move the regular-block vector-valued payload into the real `RegularPprf` sender/receiver setup rather than this local harness.
