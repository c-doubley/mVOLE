# Shared-Path Vector PPRF Prototype

## What Was Implemented

Phase 4B adds an isolated shared-path vector-valued PPRF/DPF-style correctness and benchmark test:

```bash
./build/main --MODULE_VECTOR_PPRF_TEST <preset>
```

The implementation is in `ModuleVectorPprfTest.h` and is wired through `main.cpp`.

The test models sparse additive sharing over the range

```text
K = F_{p^m}
```

represented as `m` ordinary `F_p` coordinates. For each sampled point function with punctured point `alpha` and payload `beta in K`, it generates two share vectors `y0,y1` over a domain of size `N` such that:

```text
y0[j] + y1[j] = 0       for j != alpha
y0[alpha] + y1[alpha] = beta
```

For `t` sampled point functions, the test aggregates all of them and verifies that collisions are handled by field addition at the same position.

## Shared-Path Design

This is not implemented as `m` completely independent scalar PPRFs.

For each point function, the prototype expands one shared binary seed tree to produce one leaf seed per domain point. Each leaf seed is then used to derive all `m` base-field coordinates of the range value. Thus the tree/path work is shared across the `m` coordinates, while the leaf payload work still scales with `m`.

This matches the intended RM-VOLE setup direction at the structural level: one support/path structure should carry a vector-valued payload in `F_p^m`, rather than running `m` independent scalar support generators.

## What Is Still a Prototype

This is still a local correctness and benchmark harness.

It does not yet implement:

- the real `RegularPprf` sender/receiver protocol,
- punctured-key serialization,
- correction-word communication,
- base OT/noisy VOLE setup,
- integration with `SilentWalshVoleSender` or `SilentWalshVoleReceiver`,
- malicious security,
- or ModuleMVOLE/RM-VOLE end-to-end generation.

The current implementation explicitly materializes leaf seeds and share matrices, so it should not be interpreted as optimized PPRF performance.

## Commands and Results

Build:

```bash
cmake --build build --parallel 64
```

Runs:

```bash
./build/main --MODULE_VECTOR_PPRF_TEST 1
./build/main --MODULE_VECTOR_PPRF_TEST 2
./build/main --MODULE_VECTOR_PPRF_TEST 3
./build/main --MODULE_PPRF_TEST 1
./build/main --MODULE_MVOLE 1
```

Observed output:

```text
MODULE_VECTOR_PPRF_TEST Fp64 N=1024 t=8 m=8 output_elems=8192 setup_s=0.000042 expand_s=0.000851 verify_s=0.000033 total_s=0.000925 throughput_elems_per_s=8856529.982811 shared_path=1 PASS
MODULE_VECTOR_PPRF_TEST Fp64 N=4096 t=16 m=16 output_elems=65536 setup_s=0.000318 expand_s=0.012091 verify_s=0.000189 total_s=0.012598 throughput_elems_per_s=5202258.441284 shared_path=1 PASS
MODULE_VECTOR_PPRF_TEST Fp64 N=16384 t=32 m=16 output_elems=262144 setup_s=0.001141 expand_s=0.094410 verify_s=0.000802 total_s=0.096354 throughput_elems_per_s=2720631.795473 shared_path=1 PASS
MODULE_PPRF_TEST Fp64 N=1024 t=8 m=8 domain=128 output_elems=8192 scalar_setup_s=0.000005 scalar_expand_s=0.000074 scalar_total_s=0.000079 adapter_setup_s=0.000007 adapter_expand_s=0.000181 adapter_total_s=0.000188 adapter_throughput_elems_per_s=43464970.560196 PASS
MODULE_MVOLE Fp64 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp32 N=256 t=4 m=4 PASS
```

## Timing Table

| Preset | `N` | `t` | `m` | Output elems `m*N` | Setup s | Expansion s | Verify s | Total s | Throughput elems/s | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 1024 | 8 | 8 | 8192 | 0.000042 | 0.000851 | 0.000033 | 0.000925 | 8856529.982811 | PASS |
| 2 | 4096 | 16 | 16 | 65536 | 0.000318 | 0.012091 | 0.000189 | 0.012598 | 5202258.441284 | PASS |
| 3 | 16384 | 32 | 16 | 262144 | 0.001141 | 0.094410 | 0.000802 | 0.096354 | 2720631.795473 | PASS |

## Comparison With Independent-Scalar Adapter

The existing `--MODULE_PPRF_TEST 1` adapter runs `m` independent scalar `RegularPprf` calls. On the same small parameters `N=1024,t=8,m=8`, it measured:

```text
adapter_total_s=0.000188
adapter_throughput_elems_per_s=43464970.560196
```

The new shared-path prototype measured:

```text
total_s=0.000925
throughput_elems_per_s=8856529.982811
```

So the isolated shared-path prototype is slower at this size. This is expected for this first version because it explicitly expands a binary tree and materializes local additive share matrices. The value of this phase is correctness and structure: one path/tree expansion carries all `m` coordinates of the `F_{p^m}` payload.

## Does This Support the RM-VOLE Setup Direction?

Yes, at the prototype-structure level.

The test demonstrates the desired range behavior for `K = F_{p^m}` represented as `F_p^m`, with support/path work shared across all coordinates of each payload. This is the shape needed for extension-field scalar ring-sVOLE / RM-VOLE setup:

```text
one sparse support path + one vector payload beta in F_p^m
```

instead of:

```text
m independent sparse support paths + m scalar payloads
```

The next step is not to optimize this local harness directly. The useful next step is to adapt the real `RegularPprf` sender/receiver internals so that one punctured path emits vector-valued leaf/correction payloads while preserving the existing scalar API for QA-SD VOLE.
