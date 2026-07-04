# RM-VOLE Shared-Path Vector PPRF Feasibility

Phase 6E-5 audits whether the RM-VOLE PPRF setup harness can replace the current coordinate-wise scalar `RegularPprf` adapter with a shared-path vector-valued adapter.

## Current State

The Phase 6E-4 setup harness uses:

```text
domainSize = blockSize = N/t
pointCount = t
output leaves per coordinate per sparse vector = t * blockSize = N
scalar RegularPprf instances for s = m
scalar RegularPprf instances for e = m
total scalar RegularPprf instances = 2*m
DefaultBaseOT calls = 2*m
```

This is already the correct regular-block model for libOTe `RegularPprf`: one tree per regular block and one puncture per tree. It is not `2*m*t` independent PPRFs. The remaining overhead is that the same regular-block tree/path and base OT shape is paid independently for each coordinate.

The desired shared-path model is:

```text
vector RegularPprf instances for s = 1
vector RegularPprf instances for e = 1
total vector RegularPprf instances = 2
DefaultBaseOT calls = 2
leaf payload type = m coordinates over F_p
output leaves per sparse vector = N vector leaves = m*N scalar coordinates
```

## RegularPprf API Constraints

`libOTe/libOTe/Tools/Pprf/RegularPprf.h` defines:

```cpp
template<typename F, typename G = F, typename CoeffCtx = DefaultCoeffCtx<F, G>>
class RegularPprfSender;

template<typename F, typename G = F, typename CoeffCtx = DefaultCoeffCtx<F, G>>
class RegularPprfReceiver;
```

The relevant constraints are:

- `configure(domainSize, pointCount)` sets the number of leaves per tree and the number of punctured trees.
- `baseOtCount()` returns `log2(domainSize) * pointCount`; it is independent of `sizeof(F)`.
- Sender `expand()` accepts `const VecF& value`, where `VecF = CoeffCtx::Vec<F>`, with one programmed payload per tree.
- Sender and receiver output vectors must have `domainSize * pointCount` elements for `ByTreeIndex`.
- Internal path correction uses block-valued tree seeds and base OT messages.
- Leaf values are converted from block seeds through `ctx.fromBlock()`.
- Leaf/correction arithmetic calls `ctx.plus()`, `ctx.minus()`, `ctx.copy()`, `ctx.zero()`, `ctx.serialize()`, and `ctx.deserialize()`.
- Punctured-point leaf correction payload size is controlled by `ctx.byteSize<F>()`.
- `mBaseOTs` are cleared after `expand()`, so the current API is not designed to rerun multiple expansions from the same consumed base OT object.

So `RegularPprf` does require coefficient-context arithmetic on `F`; it is not merely an opaque XOR/string PRG. However, it is generic enough to accept a vector payload type if the coefficient context provides the required operations.

## Existing Vector Support

libOTe already has an array context in `libOTe/libOTe/Tools/CoeffCtx.h`:

```cpp
template<typename G, u64 N>
struct CoeffCtxArray : CoeffCtxInteger
{
    using F = std::array<G, N>;
    void plus(F&, const F&, const F&);
    void minus(F&, const F&, const F&);
    void mul(F&, const F&, const G&);
    bool eq(const F&, const F&);
};
```

The libOTe PPRF tests instantiate:

```cpp
RegularPprfSender<std::array<u32, 11>, u32, CoeffCtxArray<u32, 11>>
RegularPprfReceiver<std::array<u32, 11>, u32, CoeffCtxArray<u32, 11>>
```

This is direct evidence that vector/array payloads are intended to work with `RegularPprf`, including `ByTreeIndex`.

For RM-VOLE over this repo's `Fp64`, `CoeffCtxArray<u64, M>` is not quite the right context because it uses native integer addition/subtraction in each coordinate rather than reduction modulo `PR = 2^61 - 1`. A Phase 6E-6 implementation should define a small project-local fixed-size context such as:

```cpp
template<u64 M>
struct RmvolePrimeVectorCtx
{
    using F = std::array<u64, M>;
    template<typename T> using Vec = AlignedUnVector<T>;

    void plus(F&, const F&, const F&);      // coordinate-wise mod p
    void minus(F&, const F&, const F&);     // coordinate-wise mod p
    void copy(F&, const F&);
    void zero(...);
    void fromBlock(F&, const block&);       // derive M reduced F_p coordinates
    u64 byteSize<F>();
    void serialize(...);
    void deserialize(...);
    bool eq(const F&, const F&);
};
```

This can live in the harness layer. It should not require modifying libOTe internals.

## Where Communication Scales

`RegularPprf` communication has two main pieces:

1. Base/path material: internal tree correction data and base OT material. This scales with `log2(blockSize) * t` per `RegularPprf` instance and does not depend on `m` except through the number of repeated scalar instances.
2. Leaf programming payload: the final leaf correction messages use `ctx.byteSize<F>()`. For scalar `F = u64`, this is 8 bytes. For vector `F = std::array<u64, M>`, this is `8*M` bytes.

Current coordinate-wise scalar mode pays the base/path material `2*m` times and sends scalar leaf corrections each time.

Shared-path vector mode would pay base/path material twice, once for `s` and once for `e`, while the leaf correction payload remains proportional to `m`. This is the right asymptotic shape for RM-VOLE setup.

For `domainSize = blockSize`, `pointCount = t`, `depth = log2(blockSize)`, and programmed points enabled, libOTe's eager expansion buffer per sparse vector is roughly:

```text
tree/path correction bytes ~= depth * t * 32
leaf correction bytes      ~= 4 * t * sizeof(F)
```

Current scalar-over-m mode for both `s` and `e`:

```text
2 * m * (depth * t * 32 + 4 * t * 8) + baseOT traffic repeated 2*m times
```

Shared vector mode for both `s` and `e`, with `sizeof(F)=8*m`:

```text
2 * (depth * t * 32 + 4 * t * 8*m) + baseOT traffic repeated 2 times
```

The expected saving is therefore mainly the repeated base/path material and repeated base OT invocations. For the current smoke points:

```text
logN=12,t=8,m=8,blockSize=512,depth=9:
  scalar RegularPprf payload shape ~= 16 instances
  vector RegularPprf payload shape ~= 2 instances
  path bytes drop by about 8x; leaf correction bytes stay m-scaled

logN=14,t=16,m=16,blockSize=1024,depth=10:
  scalar RegularPprf payload shape ~= 32 instances
  vector RegularPprf payload shape ~= 2 instances
  path bytes drop by about 16x; leaf correction bytes stay m-scaled
```

Runtime should improve less predictably than communication because vector leaf arithmetic and serialization become wider, but avoiding repeated base OT and repeated tree expansion setup should be material for larger `m`.

## Candidate Strategies

### Strategy A: Direct Fixed-Size Vector `F`

Instantiate `RegularPprf` with `F = std::array<u64, M>` and a project-local `RmvolePrimeVectorCtx<M>`.

Use `domainSize = blockSize`, `pointCount = t`, and `PprfOutputFormat::ByTreeIndex`.

For sparse vector `s`, build a `VecF beta(params.t)`, where:

```text
beta[i][h] = Delta_h * s_i mod p
```

Run one sender/receiver pair for all coordinates. The output has `params.N` vector leaves. Verification checks each coordinate:

```text
share0[j][h] + share1[j][h] = beta[i][h] at the selected leaf in block i
share0[j][h] + share1[j][h] = 0 otherwise
```

Repeat once for sparse vector `e`.

Benefits:

- No libOTe internal modification.
- Matches existing `CoeffCtxArray` precedent.
- Preserves `ByTreeIndex` indexing.
- Directly reduces instances from `2*m` to `2` for fixed supported `m`.

Risks:

- `m` must be a compile-time template parameter. The CLI must dispatch supported values, probably `m = 8, 16, 32, 64`, as `SubfieldVoleNetBench.h` already does.
- Need a prime-field vector context, not raw `CoeffCtxArray<u64, M>`.
- The context must carefully implement `fromBlock()` so every coordinate is reduced modulo `p`.
- `std::array<u64,64>` creates 512-byte leaf elements; memory use for full output is `N*m*8` bytes per party per sparse vector, plus temporaries.

### Strategy B: Reuse Scalar Tree Seeds And Derive Coordinates Locally

Run one scalar/tree RegularPprf and derive `m` coordinates from each leaf seed or leaf scalar locally.

Benefits:

- Could preserve small scalar leaf payloads if one could access raw seeds and only send vector correction at punctures.

Risks:

- RegularPprf does not expose raw leaf seeds as a public API; it converts blocks to `F` inside `expandOne()`.
- The punctured-point correction is embedded in the leaf-level OT messages, which are sized and serialized as `F`.
- A correct implementation would need either a new callback at the seed/correction layer or an internal fork of `RegularPprf`.
- This is closer to a new vector-valued PPRF implementation than a harness-level adapter.

Conclusion: good research direction, but not the lowest-risk Phase 6E-6 path.

### Strategy C: Modify libOTe RegularPprf Internals

Fork or patch `RegularPprf` so the tree/path material is scalar/block-based while leaf correction payloads are vector-valued.

Benefits:

- Potentially best optimized shape.
- Could avoid storing full vector leaves if a callback consumes leaves coordinate-wise or streams them into RM-VOLE setup state.

Risks:

- Touches libOTe internals.
- Higher security and maintenance risk.
- Requires careful compatibility with eager/non-eager send, output formats, and base OT consumption.

Conclusion: not recommended before a harness-level vector payload prototype.

### Strategy D: Batch Base OT Across Scalar Instances

Keep current scalar instances but generate all base OT material in one larger call and slice it into each `RegularPprf`.

Benefits:

- Smaller code change than vector payloads if the base OT API supports large batches cleanly.

Risks:

- Does not share tree/path correction material.
- Still expands `2*m` scalar PPRF trees.
- Less directly aligned with the RM-VOLE shared-path goal.

Conclusion: useful as a secondary optimization, but not a substitute for shared-path vector PPRF.

## Answers To Audit Questions

1. Can `RegularPprf<F,G,Ctx>` be instantiated with a field/ring type representing `F_p^m` or `F_{p^m}` payloads?
   Yes for fixed-size vector payloads. libOTe tests instantiate `F = std::array<u32,11>` with `CoeffCtxArray<u32,11>`. For RM-VOLE's prime-field coordinates, use `std::array<u64,M>` plus a custom mod-`p` vector context.

2. Does `RegularPprf` internally require arithmetic operations on `F`, or only additive/XOR-like operations and PRG output?
   It requires context operations: `fromBlock`, `plus`, `minus`, `copy`, `zero`, `serialize`, and `deserialize`. It does not require full multiplication for the PPRF path itself.

3. Where are leaf payload/correction values serialized and sent?
   In `RegularPprfSender::expandOne()` at the leaf level. The sender serializes `leafOts` into `leafMsgs` using `ctx.serialize()`, masks the bytes with PRG output derived from base OT keys, and sends the buffer through the socket. The receiver decrypts the selected message and calls `ctx.deserialize()`.

4. Is output format `ByTreeIndex` compatible with vector payloads?
   Yes. `ByTreeIndex` only indexes `VecF` elements. If each `F` is a vector payload, output element `tree * domain + leaf` is one vector leaf.

5. Could we define a small vector field wrapper type, or would RegularPprf templates/API make that impractical?
   A small fixed-size wrapper is practical. The main limitation is compile-time `M`; runtime `m` needs explicit dispatch.

6. Is there existing libOTe vector/array field support that can represent `m` coordinates?
   Yes, `CoeffCtxArray<G,N>`, but it is not a drop-in for RM-VOLE over `Fp64` because arithmetic is native coordinate arithmetic. It is a template and testing precedent, not the final field context.

7. Alternative: can we run one scalar `RegularPprf` tree and locally derive `m` coordinates from each leaf seed before applying vector correction?
   Not cleanly through the public API. Raw seed/correction hooks are internal, and the existing leaf correction messages are sized as one `F`. This would require an internal adapter/fork.

8. What code paths would need modification for shared-path vector output?
   With Strategy A, only `RmvolePprfNetSetupTest.h` needs a new vector implementation path and a small context/helper in project code. The scalar RM-VOLE expansion code and libOTe internals can remain unchanged.

9. How would communication change theoretically?
   Base/path material and base OT invocations drop from `2*m` PPRF instances to `2`. Leaf correction payloads still scale with `m` because the programmed values are vector coordinates. This should significantly reduce clean protocol bytes for larger `m`, while keeping the expected `2*m*N` scalar expanded leaves.

## Recommended Phase 6E-6 Path

Implement an isolated vector mode in `RmvolePprfNetSetupTest.h`:

```text
implementation_mode=vector-fixed-prime
```

Steps:

1. Add a fixed-size `RmvolePrimeVectorCtx<M>` using `std::array<u64,M>` and mod-`p` coordinate arithmetic.
2. Add template dispatch for `m = 8, 16, 32, 64`, mirroring the fixed-`m` dispatch pattern in `SubfieldVoleNetBench.h`.
3. Implement local verify mode first for `logN=12,t=8,m=8`.
4. Add TCP bench/verify mode after local correctness passes.
5. Print both scalar and vector counters:

```text
implementation_mode
vector_pprf_instances_s = 1
vector_pprf_instances_e = 1
total_vector_pprf_instances = 2
default_base_ot_calls = 2
expanded_vector_leaves = 2*N
expanded_scalar_coordinates = 2*m*N
```

6. Compare against the Phase 6E-4 scalar-coordinate harness using the same `logN,t,m,reps`.

Feasibility conclusion: direct vector `F` type is feasible without modifying libOTe internals. A wrapper around raw leaf seeds would require internal changes and is not the recommended next step. The practical Phase 6E-6 implementation is a fixed-size prime-vector `CoeffCtx` adapter plus harness dispatch.
