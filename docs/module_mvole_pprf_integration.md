# RM-VOLE Shared-Path Vector PPRF Integration

## What Was Added

Phase 4C adds a local RM-VOLE correctness and timing path:

```bash
./build/main --MODULE_MVOLE_PPRF <preset>
```

The implementation is in `ModuleMvolePprfIntegration.h` and is wired through `main.cpp`. It keeps the existing `--MODULE_MVOLE`, `--MODULE_MVOLE_BENCH`, `--MODULE_PPRF_TEST`, and `--MODULE_VECTOR_PPRF_TEST` behavior unchanged.

## Construction Tested

The target relation is extension-field scalar ring-sVOLE:

```text
P0: x in R_p,        Z0 in R_{p^m}
P1: Delta in F_{p^m}, Z1 in R_{p^m}

Z0 + Z1 = Delta * x
```

The implementation represents `Delta` as `m` coordinates over `F_p`, and represents `Z0,Z1` as `m` rows of length `N` over `F_p`.

Setup samples regular sparse `s,e in R_p`, a public ring element `rho`, and `Delta in F_{p^m}`. For `N,t`, the block size is `N/t`, and each block has one nonzero coefficient:

```text
alpha_i = i * (N/t) + offset_i
```

For each nonzero `s_i` at `alpha_i`, it forms the vector payload:

```text
beta_i = Delta * s_i in F_p^m
```

The regular-block shared-path vector PPRF prototype expands over the block domain `N/t`, not over the full domain `N` for every point. It generates additive shares `v0,v1` directly in the RM-VOLE row-share layout such that:

```text
v0 + v1 = sum_i beta_i * 1_{alpha_i}
```

The same process is used for `e`, producing `u0,u1` for payloads `eta_i = Delta * e_i`. The measured counter `expanded_leaves_over_N=1.000000` verifies that each sparse vector expands `N` total leaves, not `t*N`.

Expansion reuses the existing WHT-domain RM-VOLE code:

```text
P0: x  = rho * s + e
    Z0 = rho * v0 + u0

P1: Z1 = rho * v1 + u1
```

The verifier checks, for every coordinate `h` and domain position `j`:

```text
Z0[h][j] + Z1[h][j] = Delta[h] * x[j]
```

## Commands

Build:

```bash
cmake --build build --parallel 64
```

Runs:

```bash
./build/main --MODULE_MVOLE_PPRF 1
./build/main --MODULE_MVOLE_PPRF 2
./build/main --MODULE_MVOLE_PPRF 3
./build/main --MODULE_MVOLE 1
./build/main --MODULE_VECTOR_PPRF_TEST 1
```

## Observed Output

```text
MODULE_MVOLE_PPRF Fp64 N=1024 t=8 m=8 output_elems=8192 total_blocks=8 block_size=128 expanded_leaves_per_sparse_vector=1024 expected_N=1024 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.000098 pprf_e_setup_s=0.000094 expand_p0_s=0.000240 expand_p1_s=0.000197 verify_s=0.000035 total_s=0.000878 throughput_elems_per_s=9329121.720851 shared_path=1 PASS
MODULE_MVOLE Fp64 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp32 N=256 t=4 m=4 PASS
MODULE_VECTOR_PPRF_TEST Fp64 N=1024 t=8 m=8 output_elems=8192 setup_s=0.000062 expand_s=0.000835 verify_s=0.000025 total_s=0.000921 throughput_elems_per_s=8890725.908781 shared_path=1 PASS
```

## Timing Table

| Preset | `N` | `t` | `m` | Block size | Expanded leaves / sparse vector | PPRF `s` setup s | PPRF `e` setup s | Expand P0 s | Expand P1 s | Verify s | Total s | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 1024 | 8 | 8 | 128 | 1024 | 0.000098 | 0.000094 | 0.000240 | 0.000197 | 0.000035 | 0.000878 | PASS |
| 4 | 65536 | 64 | 16 | 1024 | 65536 | 0.014259 | 0.014521 | 0.032055 | 0.030789 | 0.004575 | 0.131659 | PASS |

The reported PPRF setup time includes building vector payloads and locally expanding the regular-block shared-path prototype directly into the RM-VOLE row-share layout. The `total_s` field is a wall-clock total for the full local path. It is not a real network setup/key-generation measurement.

## Prototype Caveat

This is still a local semi-honest prototype. It validates the algebra and the regular-block shared-path vector-valued PPRF setup structure, but it does not implement:

- network communication,
- base OT,
- silent setup serialization,
- sender/receiver punctured-key APIs,
- correction-word transport,
- or malicious checks.

The result supports the RM-VOLE setup direction structurally: each regular sparse block carries one shared-path vector payload in `F_p^m`. The next implementation step is to replace the local prototype with a sender/receiver integration in the real `RegularPprf`/silent setup code.
