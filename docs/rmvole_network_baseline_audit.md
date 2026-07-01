# RM-VOLE Network Baseline Audit

Phase 6A branch: `rmvole-network-baselines`

This phase audits libOTe subfield VOLE candidates and the current RM-VOLE PPRF prototype. No protocol code was changed.

## Summary

The current RM-VOLE implementation is a local correctness and cost prototype, not a real two-party network protocol. It samples both parties' setup material in one process, expands both PPRF shares locally, and then calls local `ExpandP0` and `ExpandP1`.

libOTe does provide real networked VOLE APIs over `coproto::Socket`. The closest generic candidate is:

```text
receiver: a, c
sender:   b, Delta
relation: a = b + c * Delta
```

Equivalently, since libOTe gives `a - b = c * Delta`, this can represent `Z0 + Z1 = Delta * xhat` only after an explicit sign convention such as `Z0 = -b`, `Z1 = a`, `xhat = c`. The generic libOTe template can instantiate `F` as a vector/array type and `G` as a scalar type, for example `NoisyVole<array<u32, 11>, u32, CoeffCtxArray<u32, 11>>`, but this is coordinate-wise array scaling, not the current RM-VOLE extension-field ring semantics.

## Search Results

Relevant hits:

- `libOTe/README.md`: advertises "Generic subfield noisy VOLE (semi-honest)" and "Generic subfield silent VOLE (malicious/semi-honest)" under BCGIKRS19/RRT23.
- `libOTe/libOTe/Vole/Noisy/NoisyVoleSender.h`
- `libOTe/libOTe/Vole/Noisy/NoisyVoleReceiver.h`
- `libOTe/libOTe/Vole/Silent/SilentVoleSender.h`
- `libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h`
- `libOTe/libOTe/Vole/SoftSpokenOT/SmallFieldVole.h`
- `libOTe/libOTe/Vole/SoftSpokenOT/SmallFieldVole.cpp`
- `libOTe/libOTe/Vole/SoftSpokenOT/SubspaceVole.h`
- `libOTe/libOTe/Vole/SoftSpokenOT/SubspaceVoleMaliciousLeaky.h`
- `libOTe/libOTe_Tests/Vole_Tests.cpp`
- `libOTe/libOTe_Tests/SoftSpoken_Tests.cpp`
- `libOTe/frontend/ExampleVole.cpp`
- `libOTe/frontend/benchmark.h`

No separate class/file literally named `SubfieldVole`, `BCGIKRS19`, or `RRT23` was found in implementation code. Those names appear in `README.md`; the implementation names are `NoisyVole*`, `SilentVole*`, `SmallFieldVole*`, and `SubspaceVole*`.

## libOTe Candidate APIs

### Generic noisy VOLE

Files:

- `libOTe/libOTe/Vole/Noisy/NoisyVoleSender.h`
- `libOTe/libOTe/Vole/Noisy/NoisyVoleReceiver.h`
- tests in `libOTe/libOTe_Tests/Vole_Tests.cpp`

Classes/functions:

- `NoisyVoleSender<F, G, CoeffCtx>::send(F delta, FVec& b, PRNG&, OtReceiver&, Socket&, CoeffCtx)`
- `NoisyVoleSender<F, G, CoeffCtx>::send(F delta, FVec& b, PRNG&, span<block> otMsg, Socket&, CoeffCtx)`
- `NoisyVoleReceiver<F, G, CoeffCtx>::receive(VecG& c, VecF& a, PRNG&, OtSender&, Socket&, CoeffCtx)`
- `NoisyVoleReceiver<F, G, CoeffCtx>::receive(VecG& c, VecF& a, PRNG&, span<array<block,2>> otMsg, Socket&, CoeffCtx)`

Roles:

- Sender holds `delta` and receives output vector `b`.
- Receiver chooses/holds vector `c` and receives output vector `a`.

Exact relation:

```text
a = b + c * delta
```

The test checks `a - b == delta * c`.

Field/type behavior:

- `F` is the output/correlation field type for `a`, `b`, and `delta`.
- `G` is the input/subfield type for `c`.
- `CoeffCtx` defines `plus`, `minus`, `mul`, serialization, binary decomposition of `F`, and conversion from PRG blocks.
- The test includes `F=std::array<u32, 11>`, `G=u32`, `CoeffCtxArray<u32, 11>`, where `delta` is an 11-coordinate array and `c` is a scalar. `CoeffCtxArray::mul(F&, const F&, const G&)` does coordinate-wise scaling.

Subfield/extension match:

- Yes at the API shape level: `delta` and outputs may be in `F`, while `c` is in `G`.
- Not a full `F_{p^m}` implementation by itself. For `CoeffCtxArray`, `F` is an array/vector of coordinates and multiplication by `G` is coordinate-wise scaling. That matches the evaluated coordinate relation used by this repo, but it does not implement general extension-field multiplication among two `F_{p^m}` elements.

Security:

- README advertises generic subfield noisy VOLE as semi-honest.
- No malicious check exists in `NoisyVole*`.

Choosing `N` and dimension `m`:

- `N` is the length of `c`, `a`, and `b`.
- Dimension `m` is encoded in `F` and `CoeffCtx`, for example `std::array<u32, 11>` and `CoeffCtxArray<u32, 11>`.

Network/channel API:

- Uses `coproto::Socket` and `task<>`.
- Existing tests use `cp::LocalAsyncSocket::makePair()`.
- LAN use can use `cp::asioConnect(ip, role == Role::Sender)` as shown in `libOTe/frontend/ExampleVole.cpp` for silent VOLE.
- The OT role objects are passed in explicitly, either real base/extended OT objects or precomputed OT messages.

Byte counting:

- `coproto::Socket` exposes `bytesSent()` and `bytesReceived()`.
- The older cryptoTools `Channel` exposes `resetStats()`, `getTotalDataSent()`, and `getTotalDataRecv()`.

Usability for RM-VOLE baseline:

- Usable for a small API sanity test and as a generic subfield noisy VOLE primitive.
- Not directly enough for a full RM-VOLE LAN benchmark because RM-VOLE also needs the sparse/silent/PPRF setup and ring/WHT expansion semantics.

### Generic silent VOLE

Files:

- `libOTe/libOTe/Vole/Silent/SilentVoleSender.h`
- `libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h`
- tests in `libOTe/libOTe_Tests/Vole_Tests.cpp`
- LAN example in `libOTe/frontend/ExampleVole.cpp`

Classes/functions:

- `SilentVoleSender<F, G, Ctx>::configure(requestSize, SilentBaseType, secParam, Ctx)`
- `SilentVoleReceiver<F, G, Ctx>::configure(requestSize, SilentBaseType, secParam, Ctx)`
- `SilentVoleSender<F, G, Ctx>::silentSend(F delta, VecF& b, PRNG&, Socket&)`
- `SilentVoleReceiver<F, G, Ctx>::silentReceive(VecG& c, VecF& a, PRNG&, Socket&)`
- `genSilentBaseOts`, `setSilentBaseOts`, `silentSendInplace`, `silentReceiveInplace`

Roles:

- Sender holds `delta` and gets `b`.
- Receiver gets `c` and `a`.

Exact relation:

```text
a = b + c * delta
```

The code comments use both `mA = mB + mC * mDelta` and base relation `mBaseA + mBaseB = mBaseC * mDelta` internally. The public test checks `a == b + c * d`.

Field/type behavior:

- Same template shape as noisy VOLE: `F` for `delta`, `a`, and `b`; `G` for `c`.
- The test file contains commented examples for `std::array<u32, 8>, u32, CoeffCtxArray<u32, 8>` and active tests mostly for `block, block, CoeffCtxGF128`.
- `MaliciousSupported` is restricted to `F=block` and `Ctx=CoeffCtxGF128`.

Subfield/extension match:

- API shape supports `F != G`.
- Practically, malicious mode is only implemented for GF(2^128) block.
- The generic prime/array instantiation should be treated as semi-honest only unless a malicious check is implemented for that field/context.

Security:

- Semi-honest supported generally when `ENABLE_SILENT_VOLE` is enabled.
- Malicious mode exists in the API via `mMalType = SilentSecType::Malicious`, but code throws unless `F=block` and `Ctx=CoeffCtxGF128`.

Choosing `N` and dimension `m`:

- `N` is `requestSize` passed to `configure()` or the size of the output vectors passed to `silentSend`/`silentReceive`.
- LPN/code parameters are selected by `syndromeDecodingConfigure(...)` from `requestSize`, `secParam`, and `mMultType`.
- `m` is represented by the `F` type and `Ctx`; there is no runtime `m` parameter for generic arrays beyond choosing the template instantiation.

Network/channel API:

- Uses `coproto::Socket`.
- Local tests use `cp::LocalAsyncSocket::makePair()`.
- LAN example: `libOTe/frontend/ExampleVole.cpp` uses `cp::asioConnect(ip, role == Role::Sender)`, configures sender/receiver, syncs, and calls `silentSend`/`silentReceive`.
- The protocol forks sockets internally for base OT/noisy VOLE concurrency.

Byte counting:

- `Socket::bytesSent()` and `Socket::bytesReceived()` can be read after the protocol and `flush()`.
- `frontend/benchmark.h` uses local sockets and prints `sock[0].bytesReceived()`/`sock[1].bytesReceived()` for some benchmarks.

Usability for RM-VOLE baseline:

- Usable as a networked libOTe subfield/silent VOLE baseline if instantiated with a suitable `F,G,Ctx`.
- For the evaluated RM-VOLE relation over `F_p^m` coordinates, the closest generic instantiation is an array/vector `F` with scalar `G`, but the current repo's prime-field contexts are local project code, not an audited libOTe extension-field context.
- It does not automatically benchmark the current RM-VOLE construction because RM-VOLE shares one `xhat`/base-ring input across all `m` coordinates and uses project-specific ring/WHT logic.

### SoftSpoken small-field/subspace VOLE

Files:

- `libOTe/libOTe/Vole/SoftSpokenOT/SmallFieldVole.h`
- `libOTe/libOTe/Vole/SoftSpokenOT/SmallFieldVole.cpp`
- `libOTe/libOTe/Vole/SoftSpokenOT/SubspaceVole.h`
- `libOTe/libOTe/Vole/SoftSpokenOT/SubspaceVoleMaliciousLeaky.h`
- tests in `libOTe/libOTe_Tests/SoftSpoken_Tests.cpp`

Classes/functions:

- `SmallFieldVoleSender`, `SmallFieldVoleReceiver`
- `SmallFieldVoleBase::init(fieldBits, numVoles, malicious)`
- `setBaseOts`, `expand(Socket&, PRNG&, numThreads)`
- `generate(...)`
- `SmallFieldVoleReceiver::sharedFunctionXor(...)`
- `SmallFieldVoleReceiver::sharedFunctionXorGF(...)`
- `SubspaceVoleSender<Code>`, `SubspaceVoleReceiver<Code>`
- `SubspaceVoleMaliciousSender<Code>`, `SubspaceVoleMaliciousReceiver<Code>`

Roles:

- Small-field sender outputs `u` and `v`.
- Small-field receiver holds `Delta` and outputs `w`.

Exact relation:

```text
w - v = u cdot Delta
```

where `cdot` is componentwise product. For the base small-field VOLE, `u` is over `GF(2)` and `Delta`, `v`, `w` are over `GF(2^fieldBits)`.

Field/type behavior:

- Binary subfield only: `u` over `GF(2)`.
- Extension field is `GF(2^fieldBits)` with `1 <= fieldBits <= 31`.
- Values are packed/bitsliced into `block`s.
- `sharedFunctionXorGF` additionally handles inputs over `GF(2^fieldBits)` using an explicit binary irreducible modulus.

Subfield/extension match:

- Yes for binary subfield VOLE: `x/u` in `GF(2)`, `Delta` in `GF(2^k)`.
- No for the current prime-field RM-VOLE target `xhat in F_p^N`, `Delta in F_{p^m}` with odd prime `p`.

Security:

- `SubspaceVole` is semi-honest.
- `SubspaceVoleMaliciousLeaky` adds a leaky malicious consistency check, used by SoftSpoken malicious OT.

Choosing `N` and dimension `m`:

- `fieldBits` is the extension degree over `GF(2)`.
- `numVoles` is the number of VOLE instances.
- For `SubspaceVole`, `Code(divCeil(gOtExtBaseOtCount, fieldBits))` fixes a code length, and the implementation requires `mVole.mNumVoles == code().length()`.

Network/channel API:

- Uses `coproto::Socket` in `expand`, `send`, and `recv`.
- Requires base OTs via `setBaseOts` before expansion unless managed by the higher-level SoftSpoken OT wrappers.

Byte counting:

- Same coproto socket counters: `bytesSent()` and `bytesReceived()`.

Usability for RM-VOLE baseline:

- Useful as a subfield-VOLE reference and as a binary-field baseline.
- Not a direct baseline for prime-field RM-VOLE.

## Match to Evaluated Relation

The evaluated project relation is:

```text
Z0 + Z1 = Delta * xhat
xhat in F_p^N
Delta in F_{p^m}
Z0, Z1 in (F_{p^m})^N
```

The current implementation verifies the coordinate image:

```text
Psi(Z0 + Z1)[h,j] == psi(Delta)[h] * phi(xhat)[j]
```

This is visible in `ModuleMVOLE.h::moduleMvoleVerify`.

libOTe generic noisy/silent VOLE relation:

```text
a = b + c * delta
```

Mapping is possible if `c` is `xhat`, `delta` is the coordinate/vector representation of `Delta`, and `a,b` are assigned to additive shares with an explicit sign convention, for example `Z0 = -b` and `Z1 = a`. However, libOTe's generic array context implements coordinate-wise scalar multiplication, not full project-specific RM-VOLE ring setup. Therefore it can be a networked baseline for the subfield VOLE primitive, but not evidence that current RM-VOLE is networked.

SoftSpoken small-field relation:

```text
w - v = u cdot Delta
```

This matches the subfield idea only for `GF(2) -> GF(2^k)`, not for the project's prime-field relation.

## Current RM-VOLE Network Audit

Files audited:

- `ModuleMvolePprfIntegration.h`
- `ModuleMvoleCoordPprfIntegration.h`
- `ModuleVectorPprfTest.h`
- `ModuleMVOLE.h`
- `main.cpp`

Findings:

- `ModuleVectorPprfTest.h` is a local shared-path vector PPRF prototype. `moduleVectorPprfExpandSharedTree` expands a complete tree from a local `rootSeed`; `moduleVectorPprfExpand` locally produces both `y0` and `y1`.
- `ModuleMvolePprfIntegration.h` samples base state (`rho`, `s`, `e`, `b`), `Delta`, PPRF points, root seeds, and both row-mask shares in one process.
- `ModuleMvoleCoordPprfIntegration.h` is a coordinate-wise local baseline. It repeats a scalar PPRF expansion per coordinate, but it is still local and uses locally sampled root seeds.
- `ModuleMVOLE.h` contains the local Gen/Expand/Verify algebra. `moduleMvoleGenDirect` and `moduleMvoleGenRowMasks` explicitly create both parties' masks in one process.
- `main.cpp` includes coproto headers but the RM-VOLE command paths `--MODULE_MVOLE_PPRF` and `--MODULE_MVOLE_COORD_PPRF` do not create sockets or run two roles. They call local functions directly.

Conclusion:

The current RM-VOLE_PPRF implementation is not truly networked. It is local trusted generation / local prototype code. It is valid for correctness checks, local algebra timing, and modeled cost discussion. It is not valid as a measured LAN runtime or measured communication result for RM-VOLE.

## LAN Benchmark Feasibility

Can the current setup/expand be wrapped as a two-party protocol without changing the security meaning?

No, not honestly. A wrapper around the current single-process Gen state would either reveal trusted setup material, assume a trusted dealer, or silently keep both parties' secrets in one process. That changes the security meaning and cannot be claimed as an interactive LAN protocol.

Is real PPRF setup with OT/OT extension already available?

Yes for scalar `RegularPprf` inside libOTe and the existing QA/Silent VOLE path. The project already uses `RegularPprfSender`/`RegularPprfReceiver` in the modified Walsh silent VOLE code. But the new RM-VOLE shared-path vector-valued PPRF is not integrated into the real sender/receiver PPRF protocol. It currently has no sender/receiver punctured-key API, no OT-backed setup, no correction-message serialization, and no socket API.

Minimum honest reporting for LAN experiments:

1. Networked libOTe subfield/noisy/silent VOLE baseline: measure a real libOTe two-party protocol over `coproto::Socket`/`asioConnect` and report bytes via socket counters.
2. RM-VOLE local expand plus modeled setup communication: report as a model only, clearly separated from measured LAN runtime. Include assumptions such as scalar PPRF base OT/path correction bytes and vector payload bytes.
3. Real RM-VOLE LAN runtime only after implementing networked vector-valued PPRF setup, role-separated Gen, and socket-based exchange. Until then, do not claim RM-VOLE LAN runtime.

## Recommended Phase 6B Plan

1. Add a networked libOTe VOLE baseline harness outside the existing QA_VOLE and RM-VOLE protocol code. Start with `NoisyVole<array<u64, m>, u64, CoeffCtxArray<u64, m>>` or the closest buildable fixed-`m` instantiation. Use `LocalAsyncSocket` for smoke and `asioConnect` for LAN.
2. Record `bytesSent()` and `bytesReceived()` on both coproto sockets after `flush()`. Report total bytes and per-party bytes.
3. Add a small API sanity test only if needed to prove the selected `F,G,Ctx` instantiation compiles and checks `a - b == Delta * c`.
4. Keep RM-VOLE_PPRF reported as local expand plus modeled setup. Do not label it LAN runtime.
5. Design the real vector-valued PPRF API before benchmarking RM-VOLE over LAN:
   - sender/receiver role classes,
   - OT/base setup API,
   - punctured path correction messages,
   - vector payload serialization for `m` coordinates,
   - byte counters,
   - correctness check against the current local prototype.
6. After vector PPRF is networked, split RM-VOLE setup state by role:
   - P0 should learn only `xhat`/`Z0` material,
   - P1 should learn only `Delta`/`Z1` material,
   - no trusted all-seeing `ModuleMVOLEGenState` in measured protocol code.

Recommended next command:

```bash
git status --short && cmake --build build --parallel $(nproc) && ./build/main --MODULE_MVOLE_PPRF 1 && ./build/main --MODULE_MVOLE_COORD_PPRF 1 && ./build/main --QA_VOLE 14
```

## Minimal Existing Tests Run

Commands run:

```bash
./libOTe/out/build/linux/frontend/frontend_libOTe -u 115 137 138 141 143
./libOTe/out/build/linux/frontend/frontend_libOTe -u 140
```

Results:

- `115 - Vole_SoftSpokenSmall_Test`: passed.
- `137 - Vole_Noisy_test`: passed.
- `138 - Vole_Silent_paramSweep_test`: passed, but this test is effectively empty in this checkout.
- `141 - Vole_Silent_baseOT_test`: passed, but this test is effectively empty in this checkout.
- `143 - Vole_Silent_Rounds_test`: passed, but this test is effectively empty in this checkout.
- `140 - Vole_Silent_QuasiCyclic_test`: skipped because `ENABLE_BITPOLYMUL` is not defined.

I did not run the larger `Vole_Silent_Tungsten_test` or `Vole_Silent_mal_test` in this audit phase.
