# ModuleMVOLE PPRF Adapter Notes

## Files inspected

| File | Notes |
| --- | --- |
| `SilentWalshVoleSender.h` | Uses `RegularPprfSender<F, G, Ctx> mGen`. `configure()` fixes `mNumPartitions = 64`, computes `mSizePer = requestSize / 64`, and calls `mGen.configure(mSizePer, mNumPartitions)`. |
| `SilentWalshVoleReceiver.h` | Uses `RegularPprfReceiver<F, G, Ctx> mGen`. Samples PPRF choice bits, stores sparse positions from `mGen.getPoints(...)`, and expands into `mA`. |
| `libOTe/libOTe/Tools/Pprf/RegularPprf.h` | Implements scalar-output regular PPRF sender/receiver. Each tree has one punctured point; output element type is the template field type `F`. |
| `libOTe/libOTe/Tools/Pprf/PprfUtil.h` | Defines output layouts: `ByLeafIndex`, `ByTreeIndex`, `Interleaved`, and `Callback`. |
| `Prime_test.h` | `VOLE_prime_QASD` runs two silent Walsh VOLE calls for `s` and `e`, then verifies the scalar QA-SD VOLE relation. |

## Adapter design

Phase 4A adds an isolated test flag:

```bash
./build/main --MODULE_PPRF_TEST 1
```

The test is implemented in `ModulePprfTest.h` and wired through `main.cpp`.

For preset `1`:

| Parameter | Value |
| --- | ---: |
| `N` | 1024 |
| `t` | 8 |
| `m` | 8 |
| PPRF domain | `N / t = 128` |
| PPRF trees | `t = 8` |
| field | Fp64 |

The adapter is intentionally simple: it runs `m` independent scalar `RegularPprf` instances with domain-separated seeds. This is an upper-bound baseline for vector-valued output, not the final shared-path design.

The test uses `PprfOutputFormat::ByTreeIndex`, so index `tree * domain + leaf` corresponds to one tree and one leaf. For each scalar coordinate it checks:

```text
receiverOut[tree, leaf] == senderOut[tree, leaf]                    if leaf is not punctured
receiverOut[tree, leaf] == senderOut[tree, leaf] + delta[tree]      if leaf is punctured
```

This verifies that non-punctured evaluations agree and that punctured positions are handled through the programmed correction value.

## Build and test

Build:

```bash
cmake --build build --parallel 64
```

Command:

```bash
./build/main --MODULE_PPRF_TEST 1
```

Observed output:

```text
MODULE_PPRF_TEST Fp64 N=1024 t=8 m=8 domain=128 output_elems=8192 scalar_setup_s=0.000005 scalar_expand_s=0.000077 scalar_total_s=0.000082 adapter_setup_s=0.000007 adapter_expand_s=0.000185 adapter_total_s=0.000192 adapter_throughput_elems_per_s=42713580.318785 PASS
```

Regression check:

```bash
./build/main --MODULE_MVOLE 1
```

Output:

```text
MODULE_MVOLE Fp64 N=256 t=4 m=4 PASS
MODULE_MVOLE Fp32 N=256 t=4 m=4 PASS
```

## Can RegularPprf directly support shared-path vector output?

Not directly in its current public API.

`RegularPprfSender<F, G, Ctx>` and `RegularPprfReceiver<F, G, Ctx>` are templated around a scalar field element type `F`. The leaf hashing path writes one `F` per leaf, and the correction buffer serialization uses `ctx.byteSize<F>()`. There is a `Callback` output format, but the callback receives a scalar `VecF` leaf layer, not a vector-valued leaf payload.

The current API can support vector-valued output only by:

- running `m` independent scalar PPRFs, as this Phase 4A adapter does, or
- defining a new field/container type whose serialized element is an `m`-tuple and whose `CoeffCtx` implements the required arithmetic and serialization.

The second option would be invasive and is not the same as a clean shared-path vector-valued PPRF over ordinary `F_p^m`.

## What an optimized vector PPRF would need

An optimized shared-path vector-valued PPRF should keep one GGM tree path per punctured point and emit `m` field elements at each leaf. Concretely, it would need:

1. A leaf expansion interface that derives `m` field elements from each leaf seed using domain separation.
2. Correction messages that carry `m` field elements for each programmed punctured value.
3. Output buffers shaped as either `m` scalar planes or packed `N * m` field elements.
4. Sender/receiver verification and serialization logic that treats the correction value as `Delta[0..m-1]` per punctured point.
5. A clean way to preserve existing scalar PPRF behavior for QA-SD VOLE.

That would reduce shared tree traversal and base OT path cost compared with `m` independent scalar PPRFs.

## Cost comparison discussion

### `m` independent QA-SD VOLEs

Running `m` independent QA-SD VOLE instances is the most direct baseline. It repeats the silent VOLE/PPRF setup and expansion for every row coordinate. Cost should scale roughly linearly in `m`, and it also repeats the surrounding QA-SD wrapper work.

### Current ModuleMVOLE algebra/WHT layer

The optimized local algebra/WHT layer from Phase 3E is already small compared with repeated protocol setup. For example, `N = 2^18`, `m = 16` completed in about `0.515s` including verification and produced `4,194,304` output field elements. This layer does not yet include PPRF/silent setup.

### ModuleMVOLE with independent scalar PPRFs

The Phase 4A adapter is an upper-bound baseline: it performs `m` independent scalar PPRF expansions. For `N = 1024`, `t = 8`, `m = 8`, the isolated adapter took about `0.000192s` total. This is useful as a correctness and API baseline, but it will repeat tree/base material work across coordinates and therefore should scale roughly linearly in `m`.

### Ideal shared-path vector-valued PPRF

The target design is one PPRF tree/path structure with vector-valued leaves and vector-valued puncture corrections. Ideally, the tree traversal and base OT cost are paid once per punctured point, while only the leaf hashing/correction payload scales with `m`. That is the design most likely to make ModuleMVOLE faster than running `m` full QA-SD VOLE instances.

## Phase 4A conclusion

The existing PPRF code is usable today for an isolated vector-valued adapter via `m` independent scalar PPRF calls. It does not expose a direct shared-path vector-valued output API. The next implementation step should be a small shared-path adapter experiment around `RegularPprf` internals or a new wrapper that preserves the existing scalar API while adding vector leaf/correction handling.
