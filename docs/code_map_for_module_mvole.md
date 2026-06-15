# Code Map for Module-Valued Matrix-VOLE

Date: 2026-06-10

Repository root inspected: `/home/cyy/pcg-experiments`

Note: this checkout is rooted directly at `/home/cyy/pcg-experiments`; there is no nested `whtpcg/` directory in this workspace.

## Implementation Map

| Purpose | File | Function/Class | Notes |
|---|---|---|---|
| Top-level benchmark CLI | `main.cpp` | `main`, `printUsage` | Parses `argv[1]` and `argv[2]`; computes `n = 2^num_var`. Flags include `--QA_Syndrome`, `--EA_Syndrome`, `--EC_Syndrome`, `--QA_VOLE`, `--EA_VOLE`, `--EC_VOLE`, and `--OLE`. |
| QA syndrome benchmark dispatch | `main.cpp` | `--QA_Syndrome` branch | Calls `QA_prime_encode_test<u64, CoeffCtxIntegerPrime_64>(n)` and `QA_prime_encode_test<u32, CoeffCtxIntegerPrime_32>(n)`. |
| QA-SD VOLE benchmark dispatch | `main.cpp` | `--QA_VOLE` branch | Calls `VOLE_prime_QASD<u64, CoeffCtxIntegerPrime_64>(n)` and `VOLE_prime_QASD<u32, CoeffCtxIntegerPrime_32>(n)`. |
| 64-bit prime-field context | `Coeff128.h` | `osuCrypto::CoeffCtxIntegerPrime_64` | Field element type used by call sites is `u64`. Prime is `PR = 2305843009213693951` (`2^61 - 1`). Provides `plus`, `minus`, `mul`, `eq`, `fromBlock`, `powerOfTwo`, `zero`, `one`, `copy`, serialization helpers, and `Vec<F> = AlignedUnVector<F>`. Multiplication uses `__int128_t` before reducing modulo `PR`. |
| 32-bit prime-field context | `Coeff128.h` | `osuCrypto::CoeffCtxIntegerPrime_32` | Field element type used by call sites is `u32`. Prime is `PR = 2147483647` (`2^31 - 1`). Provides the same context shape as the 64-bit context. Multiplication uses `u_int64_t` before reducing modulo `PR`. |
| Generic libOTe coefficient context model | `libOTe/libOTe/Tools/CoeffCtx.h` | `CoeffCtxInteger` | Shows the interface expected by libOTe protocols: arithmetic operations, `binaryDecomposition`, `fromBlock`, `Vec<F>`, resize/copy/zero/serialize helpers. The project prime contexts mirror this shape. |
| Vector representation | `Coeff128.h`, `libOTe/libOTe/Tools/CoeffCtx.h` | `template<typename F> using Vec = AlignedUnVector<F>` | QA-SD VOLE vectors are `Ctx::Vec<F>`, i.e. aligned vectors with `size`, `operator[]`, `begin`, and `end`. WHT functions mutate this vector type in-place. |
| Matrix representation | `libOTe/cryptoTools/cryptoTools/Common/Matrix.h` | `osuCrypto::Matrix<T>` | Existing row/column matrix type. It inherits `MatrixView<T>`, supports `resize(rows, cols)`, `operator()(row, col)`, `operator[]` row access via `MatrixView`, contiguous storage, copy/move, and equality. Existing OLE/DPF code uses `Matrix<F>` for large expanded outputs. This is a natural type for `Z0`/`Z1` in ModuleMVOLE. |
| WHT implementation | `Walsh.h` | `wht<F, Ctx>(typename Ctx::template Vec<F>& coeffs, int len)` | In-place Walsh-Hadamard transform over `Ctx` arithmetic. `coeffs` length should be `len`, normally a power of two. Each butterfly stores `u+v` and `u-v` back into `coeffs`. Used by QA syndrome and Walsh VOLE expansion. |
| WHT implementation for `std::vector` | `Walsh.h` | `prime_wht<F, Ctx>(std::vector<F>& coeffs, int len)` | Same in-place butterfly logic but takes `std::vector<F>&`. Not used by the observed QA-SD VOLE path, but useful for prototype helper code if not using `AlignedUnVector`. |
| QA syndrome benchmark body | `Prime_test.h` | `QA_prime_encode_test<F, CoeffCtx>` | Allocates `VecF coeff(n), a(n)`, samples both with `mctx.fromBlock`, applies `wht` to both in-place, then pointwise multiplies `a[i] = coeff[i] * a[i]`. Prints timing only. |
| QA-SD VOLE wrapper/test | `Prime_test.h` | `VOLE_prime_QASD<F, Ctx>(u64 n)` | Allocates two silent VOLE instances, samples public vector `a`, runs two VOLEs to obtain shares for `s` and `e`, computes `Vol[i] = a[i] * s[i] + e[i]`, computes additive shares `addshare1`, `addshare2`, times threads, and checks `d * Vol[i] + addshare2[i] == addshare1[i]`. |
| Public ring/vector element in QA-SD wrapper | `Prime_test.h` | local `VecF a(n)` in `VOLE_prime_QASD` | This is the prototype's public coefficient/evaluation vector currently named `a`. It is sampled elementwise with `ctx.fromBlock(a[i], prng.get())`. The line `wht<F, Ctx>(a,n)` is present but commented out, so the current wrapper treats sampled `a` directly in the VOLE relation rather than explicitly deriving it from a ring multiplication in this function. |
| Delta in QA-SD wrapper | `Prime_test.h` | local `F d` in `VOLE_prime_QASD` | Sampled with `ctx.fromBlock(d, prng.get())`. Passed to sender as the VOLE correlation scalar for both `s` and `e`: `send.silentSend(d, s2, ...)`, `send.silentSend(d, e2, ...)`. |
| Sparse/noise vectors in QA-SD wrapper | `Prime_test.h` | locals `s`, `e`, `s1`, `e1`, `s2`, `e2` | `recv.silentReceive(s, s1, ...)` gives receiver-side vector `s` and additive share `s1`; sender receives `s2`; relation checked as `s1 == s2 + d*s`. Same pattern for `e`. |
| QA-SD VOLE sender class | `SilentWalshVoleSender.h` | `SilentWalshVoleSender<F, G, Ctx>` | Modified silent VOLE sender for Walsh compression over prime fields. Owns `RegularPprfSender<F, G, Ctx> mGen`, `VecF mBaseB`, and expanded sender share `VecF mB`. |
| Sender configure logic | `SilentWalshVoleSender.h` | `configure(requestSize, type, secParam, ctx)` | Hardcodes `mNumPartitions = 64`, sets `mNoiseVecSize = requestSize`, and computes `mSizePer = mNoiseVecSize / mNumPartitions`. Calls `mGen.configure(mSizePer, mNumPartitions)`. |
| Sender base generation/setup | `SilentWalshVoleSender.h` | `genSilentBaseOts`, `setSilentBaseOts` | Uses `NoisyVoleSender` plus base/extended OT to get base correlations. `setSilentBaseOts` passes base OTs into `mGen.setBase`, stores the negated sender base share in `mBaseB`. |
| Sender expansion | `SilentWalshVoleSender.h` | `silentSend`, `silentSendInplace` | `silentSend` calls `silentSendInplace`, copies `mB` to caller output, and clears state. `silentSendInplace` configures if needed, generates base OTs if missing, allocates `mB`, programs PPRF with `baseB`, calls `mGen.expand(..., PprfOutputFormat::Interleaved, true, 1)`, then applies WHT in-place for non-`block` fields. |
| Sender WHT/compression step | `SilentWalshVoleSender.h` | `silentSendInplace` | For prime fields (`G != block`), calls `wht<G, Ctx>(mB, mB.size())` and resizes to `mRequestSize`. For `block`, it uses `foleageFft` instead. |
| QA-SD VOLE receiver class | `SilentWalshVoleReceiver.h` | `SilentWalshVoleReceiver<F, G, Ctx>` | Modified silent VOLE receiver for Walsh compression over prime fields. Owns `RegularPprfReceiver<F, G, Ctx> mGen`, point positions `mS`, base sparse values `mBaseC`, base share `mBaseA`, expanded `mA`, and sparse vector `mC`. |
| Receiver configure logic | `SilentWalshVoleReceiver.h` | `configure(requestSize, type, secParam, ctx)` | Hardcodes `mNumPartitions = 64`, sets `mNoiseVecSize = requestSize`, computes `mSizePer = mNoiseVecSize / mNumPartitions`, and calls `mGen.configure(mSizePer, mNumPartitions)`. |
| Receiver sparse support/value sampling | `SilentWalshVoleReceiver.h` | `sampleBaseChoiceBits`, `sampleBaseVoleVals` | `sampleBaseChoiceBits` delegates to `mGen.sampleChoiceBits(prng)`. `sampleBaseVoleVals` samples nonzero base values into `mBaseC`, calls `mGen.getPoints(mS, PprfOutputFormat::Interleaved)`, and stores sparse support positions in `mS`. |
| Receiver base generation/setup | `SilentWalshVoleReceiver.h` | `genSilentBaseOts`, `setSilentBaseOts` | Uses `NoisyVoleReceiver` plus base/extended OT to generate base correlations. `setSilentBaseOts` calls `mGen.setBase` and stores base share `mBaseA`. |
| Receiver expansion | `SilentWalshVoleReceiver.h` | `silentReceive`, `silentReceiveInplace` | `silentReceive` calls `silentReceiveInplace`, copies `mC` and `mA` to caller outputs, and clears state. `silentReceiveInplace` configures if needed, generates base OTs if missing, allocates `mA` and sparse `mC`, expands PPRF into `mA`, places sparse nonzero values in `mC` at `mS`, adds `mBaseA` into `mA` at active points, then applies WHT in-place to both `mA` and `mC` for non-`block` fields. |
| Receiver WHT/compression step | `SilentWalshVoleReceiver.h` | `silentReceiveInplace` | For prime fields (`G != block`), calls `wht<G, Ctx>(mA, mA.size())` and `wht<G, Ctx>(mC, mC.size())`, then resizes both to `mRequestSize`. This is the point where sparse ring vectors become their WHT/evaluation-domain outputs. |
| PPRF sender implementation | `libOTe/libOTe/Tools/Pprf/RegularPprf.h` | `RegularPprfSender<F, G, CoeffCtx>` | Multi-point punctured PRF sender. Stores `mDomain`, `mDepth`, `mPntCount`, `mValue`, and `Matrix<std::array<block, 2>> mBaseOTs`. |
| PPRF receiver implementation | `libOTe/libOTe/Tools/Pprf/RegularPprf.h` | `RegularPprfReceiver<F, G, CoeffCtx>` | Multi-point punctured PRF receiver. Stores `mDomain`, `mDepth`, `mPntCount`, `Matrix<block> mBaseOTs`, and `Matrix<u8> mBaseChoices`. |
| PPRF configuration | `libOTe/libOTe/Tools/Pprf/RegularPprf.h` | `RegularPprfSender::configure`, `RegularPprfReceiver::configure` | Both require `domainSize` to be even and at least 2. They set `mDomain = domainSize`, `mDepth = log2ceil(mDomain)`, `mPntCount = pointCount`. Base OT count is `mDepth * mPntCount`. |
| PPRF point sampling and index mapping | `libOTe/libOTe/Tools/Pprf/RegularPprf.h` | `RegularPprfReceiver::sampleChoiceBits`, `getPoints` | Samples one active point per tree in `[0, mDomain)`. In interleaved output format, maps points to `(batch * mDomain + point) * 8 + subtree`, matching the batched-by-8 output layout. |
| PPRF sender expansion | `libOTe/libOTe/Tools/Pprf/RegularPprf.h` | `RegularPprfSender::expand`, `expandOne` | Validates output format, optionally programs punctured point values with `setValue`, expands GGM trees in batches of 8, and sends correction buffers. In interleaved mode, leaf output is written directly into caller-provided output. |
| PPRF receiver expansion | `libOTe/libOTe/Tools/Pprf/RegularPprf.h` | `RegularPprfReceiver::expand`, `expandOne` | Validates output format, receives correction buffers, expands the receiver side of the PPRF, and writes output directly in interleaved mode. |
| Cause of `--QA_VOLE 1` / `--QA_VOLE 6` failure | `SilentWalshVoleSender.h`, `SilentWalshVoleReceiver.h`, `RegularPprf.h` | `mSizePer = requestSize / 64`; `mGen.configure(mSizePer, 64)` | `main.cpp` passes `requestSize = 2^n`. With `n=1`, request size is 2, so `mSizePer = 0`, rejected by PPRF because domain `< 2`. With `n=6`, request size is 64, so `mSizePer = 1`, rejected because domain is odd and `< 2`/not a valid PPRF domain. The smallest passing value is `n=7`, request size 128, `mSizePer = 2`. |
| Existing prime OLE/DPF matrix usage | `Prime_OLE.h` | `RegularPrimeDpf_Test`, `prime_dpf`, `Prime_OLE` | Uses `std::array<Matrix<F>, 2> output` and `std::array<Matrix<u8>, 2> tags` for expanded DPF tables. This is the closest existing example for storing `m x N` field-element matrices. |
| Existing DPF implementation | `Dpf.h` | `RegularPrimeDpf` | Uses `Matrix<block>`, `Matrix<u8>`, and `CoeffCtxIntegerPrime_64`. Not directly QA-SD VOLE, but useful as a style reference for callbacks and matrix-shaped expanded outputs. |
| libOTe benchmark framework | `libOTe/frontend/benchmark.h` | `PprfBench`, `VoleBench2`, `benchmark` | Contains upstream libOTe benchmark entry points for PPRF and silent VOLE. The WHTPCG artifact's top-level benchmark entry point is `main.cpp`; this file is useful if later comparing against libOTe primitives. |
| libOTe unit test registry | `libOTe/libOTe_Tests/UnitTests.cpp` | `UnitTests` registry | Lists unit tests including PPRF tests and noisy/silent VOLE tests. Phase 1 used PPRF test 94 and noisy VOLE test 137. |

## QA-SD VOLE Flow Summary

The artifact's QA-SD VOLE path is exposed through `./build/main --QA_VOLE n`, which passes `N = 2^n` into `VOLE_prime_QASD`.

`VOLE_prime_QASD` samples:

- `d`: scalar VOLE correlation held by the sender side.
- `a`: public length-`N` field vector used as the apparent public ring/evaluation vector in the wrapper.
- `s`, `e`: receiver-side vectors from two silent VOLE calls.
- `s1`, `e1`: receiver additive shares of `d*s` and `d*e`.
- `s2`, `e2`: sender additive shares of `d*s` and `d*e`.

It then forms:

```text
Vol[i]       = a[i] * s[i] + e[i]
addshare1[i] = a[i] * s1[i] + e1[i]
addshare2[i] = a[i] * s2[i] + e2[i]
```

and checks:

```text
d * Vol[i] + addshare2[i] == addshare1[i]
```

Inside `SilentWalshVoleReceiver`, sparse support is generated by the PPRF receiver (`mGen.getPoints(mS, Interleaved)`), nonzero sparse values are sampled into `mBaseC`, and WHT is applied to both `mA` and `mC`. Inside `SilentWalshVoleSender`, PPRF output is programmed using base shares and WHT is applied to `mB`.

## Suggested Extension Points for ModuleMVOLE

Minimum files/functions to touch later:

| Purpose | File | Function/Class | Notes |
|---|---|---|---|
| Add prototype API and core logic | New file, e.g. `ModuleMVOLE.h` | `ModuleMVOLEParams`, `P0Output`, `P1Output`, `runModuleMVOLE` or similar | Keep this separate from existing source initially. Use `CoeffCtxIntegerPrime_64` / `_32`, `Ctx::Vec<F>`, and `Matrix<F>` for outputs. |
| Reuse WHT row-wise | `Walsh.h` or new helper in `ModuleMVOLE.h` | `wht<F, Ctx>` | Implement `WHT_m` by applying `wht` independently to each row vector. If using `Matrix<F>`, copy each row into `Ctx::Vec<F>` or iterate contiguous row spans if `MatrixView` access is convenient. |
| Reuse field arithmetic | `Coeff128.h` | `CoeffCtxIntegerPrime_64`, `CoeffCtxIntegerPrime_32` | Use `ctx.plus`, `ctx.minus`, `ctx.mul`, `ctx.fromBlock`, `ctx.eq`, and `ctx.zero`. Do not introduce a new field type unless needed. |
| Reuse matrix storage | `libOTe/cryptoTools/cryptoTools/Common/Matrix.h` | `Matrix<F>` | Store `Z0` and `Z1` as `Matrix<F>(m, N)`. Access as `Z(h, j)` or row indexing style used by existing code. |
| Reuse QA-SD scalar VOLE path | `Prime_test.h`, `SilentWalshVoleSender.h`, `SilentWalshVoleReceiver.h` | `VOLE_prime_QASD`, `silentSend`, `silentReceive` | For a first prototype, mirror `VOLE_prime_QASD` structure but lift scalar `d` to a vector `Delta[h]`. Avoid changing these existing classes until the prototype test is clear. |
| Module-valued PPRF or direct sharing prototype | New file first; later possibly `SilentWalshVoleSender.h` / `SilentWalshVoleReceiver.h` | `Delta * s`, `Delta * e` row generation | The target needs shares of `Delta[h] * s[j]` and `Delta[h] * e[j]` for each row `h`. Minimal clear prototype can loop over `h` and reuse scalar operations with domain separation or direct sampled masks. If optimizing later, vector-valued PPRF can be added around `RegularPprf` using domain-separated outputs. |
| Correctness test entry | New test file or guarded CLI branch in `main.cpp` later | `--ModuleMVOLE_Test` or standalone test binary | The user asked not to implement yet. Later, add small tests for `N=2^8`/`2^10`, `m={2,4,8}`, checking `Z0(h,j)+Z1(h,j)==Delta[h]*x[j]`. |
| Benchmark entry | Later: `main.cpp` or separate benchmark source | `--ModuleMVOLE` | Keep separate from correctness first. Report setup, expansion, WHT, total, `mN`, throughput. |

For the requested correlation:

```text
P0: x in F_p^N, Z0 in F_p^{m x N}
P1: Delta in F_p^m, Z1 in F_p^{m x N}
Z0 + Z1 = Delta * x^T
```

the lowest-risk implementation path is:

1. Create a new `ModuleMVOLE.h` that does not alter existing QA-SD VOLE files.
2. Use `Ctx::Vec<F>` for `x`, `s`, `e`, row temporaries, and `Delta`.
3. Use `Matrix<F>` for `Z0` and `Z1`.
4. Reproduce the algebra of `VOLE_prime_QASD`, but for each `h` compute shares for `Delta[h] * s` and `Delta[h] * e`.
5. Apply `wht` independently to each row of the module share matrices.
6. Verify every coordinate with `ctx.plus(lhs, Z0(h,j), Z1(h,j))` and `ctx.mul(rhs, Delta[h], x[j])`.

Do not implement `F_{p^m}` arithmetic; treat the module as `R_p^m` and keep `Delta` as `m` independent field elements.
