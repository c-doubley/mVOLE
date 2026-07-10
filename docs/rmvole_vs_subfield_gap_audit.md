# Phase 6H: RM-VOLE vs libOTe Noisy Subfield VOLE Gap Audit

## Bottom Line

The Phase 6G headline communication reduction is **not an apples-to-apples SOTA comparison**.

The measured numbers are internally consistent, but they compare:

- an RM-VOLE harness that measures **real TCP `DefaultBaseOT` + vector-valued `RegularPprf` setup plus local WHT expansion**, while excluding centralized harness metadata; against
- libOTe **noisy** generic subfield VOLE instantiated as `F = std::array<u8, m>`, `G = u8`, where the dominant noisy payload scales as `O(m^2 N)`.

The result is useful as an external stress baseline for the current libOTe noisy subfield API, but it should not be described as a final communication advantage over comparable PCG baselines. The current RM-VOLE benchmark still omits the secure split-input protocol needed to generate/program `Delta*s_i` and `Delta*e_i` when `P0` owns `s,e` and `P1` owns `Delta`.

## 1. What libOTe Noisy Subfield VOLE Instantiates

The Phase 6G libOTe baseline is implemented in `SubfieldVoleNetBench.h` as:

```cpp
using F = std::array<u8, M>;
using G = u8;
using Ctx = CoeffCtxArray<G, M>;
NoisyVoleReceiver<F, G, Ctx> receiver;
NoisyVoleSender<F, G, Ctx> sender;
```

The relation is the libOTe noisy VOLE relation:

```text
receiver outputs (a, c)
sender outputs (b, Delta)
a = b + c * Delta
```

Phase 6G maps this to the evaluated sign convention:

```text
xhat = c
Z1 = a
Z0 = -b
Z0 + Z1 = c * Delta
```

Important type details:

- `F` is an array of `m` bytes, not this repo's `F_{p^m}` representation.
- `G` is one byte.
- `CoeffCtxArray<u8,m>::mul(F&, const F&, const G&)` multiplies each byte coordinate by the scalar byte.
- `m` is a C++ array length in bytes/coordinates. It is not a true extension-field degree with full `F_{p^m}` multiplication.
- Each repetition generates `N` VOLE pairs over `F`; each `F` output contains `m` byte coordinates.

The libOTe implementation explains the scaling. `NoisyVoleReceiver::receive()` sizes the message vector as:

```cpp
ctx.resize(msg, otMsg.size() * a.size());
```

and sends:

```cpp
buff.resize(msg.size() * ctx.template byteSize<F>());
```

For this instantiation:

```text
otMsg.size()      = ctx.bitSize<F>() = sizeof(F) * 8 = 8m
a.size()          = N
byteSize<F>()     = sizeof(F) = m
dominant payload  = 8m * N * m = 8 m^2 N bytes per repetition
```

Base OT and framing add a small extra term, but the dominant term is quadratic in `m`.

## 2. Is `entries = m*N` Valid?

It is valid only for a narrow interpretation: `entries = m*N` counts byte-sized coordinates produced by the chosen `std::array<u8,m>` representation.

It is **not** valid as a same-field comparison to RM-VOLE's `Fp64` coordinates, and it is **not** a count of generated extension-field elements. Better normalizations are:

| Normalization | Meaning for libOTe noisy `array<u8,m>` | One-repetition dominant communication |
|---|---|---:|
| Per generated `F` element | `N` elements, each an array of `m` bytes | `8m^2` bytes per `F` element |
| Per output byte coordinate | `m*N` byte coordinates | `8m` bytes per output byte |
| Per output bit | `8m*N` output bits | `m` bytes per output bit |
| Per RM-VOLE `Fp64` coordinate equivalent | Treat 8 byte coordinates as one 64-bit coordinate, only as a rough unit conversion | `64m` bytes per 64-bit coordinate |

Phase 6G TCP rows used `reps=3`. The CSV communication counters are cumulative over the three repetitions, while `entries = m*N` is per logical point. Therefore the displayed bytes/entry are about three times the one-repetition byte-coordinate cost. This affects absolute bytes/entry for both methods; because both Phase 6G methods used `reps=3`, the RM-vs-libOTe communication ratios are not changed by this convention, but the axis label should say that communication is cumulative over three repetitions unless the CSV is renormalized.

## 3. Why libOTe Shows 192, 384, 768, 1536 Bytes/Entry

From Phase 6G:

| m | N example | libOTe total bytes | CSV entries | CSV bytes/entry |
|---:|---:|---:|---:|---:|
| 8 | 4096 | 6,298,176 | 32,768 | 192.205 |
| 16 | 4096 | 25,179,072 | 65,536 | 384.202 |
| 32 | 4096 | 100,689,600 | 131,072 | 768.201 |
| 64 | 4096 | 402,705,600 | 262,144 | 1536.200 |

The dominant one-repetition formula is:

```text
bytes ~= 8 m^2 N
```

Phase 6G records three repetitions:

```text
bytes ~= 3 * 8 m^2 N = 24 m^2 N
```

Dividing by `entries = m*N` gives:

```text
bytes/entry ~= 24m
```

So:

```text
m=8   -> 24*8   = 192
m=16  -> 24*16  = 384
m=32  -> 24*32  = 768
m=64  -> 24*64  = 1536
```

The observed values match this almost exactly. Yes: the measured libOTe noisy communication scales like `O(m^2 N)` for this `F = array<u8,m>` instantiation.

This is expected from noisy VOLE's binary decomposition of the sender's `Delta`: `bitSize<F>() = 8m`, and for every decomposition bit the receiver sends an `F`-sized correction for every one of the `N` outputs.

## 4. What RM-VOLE Counts

`RmvoleNetBench.h` measures:

- two vector-valued `RegularPprf` executions:
  - one for sparse `Delta*s`;
  - one for sparse `Delta*e`;
- `DefaultBaseOT` before each vector `RegularPprf`;
- `RegularPprf` correction/path messages;
- local deterministic expansion:
  - `P0`: `x = WHT(rho*s + e)`, `Z0 = WHT(rho*v0) + WHT(u0)`;
  - `P1`: `Z1 = WHT(rho*v1) + WHT(u1)`.

The CSV field `clean_protocol_bytes` includes the TCP socket bytes for:

```text
DefaultBaseOT + RegularPprf setup for Delta*s
DefaultBaseOT + RegularPprf setup for Delta*e
```

It excludes:

- centralized harness metadata (`rhoSeed`, `Delta`, sparse offsets);
- verification openings;
- any secure protocol for generating `Delta*s_i` and `Delta*e_i` from split inputs;
- any reverse VOLE/input-setup cost;
- malicious checks.

The RM-VOLE setup communication is closer to:

```text
O(baseOT(t, blockSize) + vector-PPRF corrections with m-coordinate payloads)
```

not `O(m^2 N)`. The vector PPRF shares the tree/path structure across the `m` coordinates, so it is expected to scale much better in `m` than the noisy `array<u8,m>` baseline. That part of the measurement is real and useful.

However, it is not the whole protocol cost.

## 5. Missing RM-VOLE Costs For A Real Protocol

The current RM-VOLE harness centrally samples the test instance. In the intended protocol:

```text
P0 conceptually owns sparse s and e.
P1 conceptually owns Delta.
```

For each support value:

```text
betaS_{i,h} = Delta_h * s_i
betaE_{i,h} = Delta_h * e_i
```

But `RegularPprfSender::expand()` is programmed with `beta`. In the current bench path, the sender side can compute or sample those programmed payloads because the harness centrally creates the full instance. That is not secure input generation.

A real two-party setup needs an additional split-input product/share-generation component so that:

- `P0` does not learn `Delta`;
- `P1` does not learn `s_i`, `e_i`, or sparse supports beyond what the construction allows;
- the parties obtain additive shares of `Delta*s_i` and `Delta*e_i` suitable for PPRF programming/opening.

Likely missing components include one or more of:

- reverse VOLE for the sparse support payloads;
- a small multiplication protocol for `Delta_h * s_i` and `Delta_h * e_i`;
- a secure way to program vector PPRF corrections from secret-shared payloads;
- input consistency/checking if moving beyond semi-honest;
- accounting for any base VOLE or OT extension used by that setup.

Until these costs are modeled or implemented, `RMVOLE-vector-RegularPprf-setup-local-expand` is a **networked setup subroutine plus local expansion benchmark**, not a complete RM-VOLE LAN runtime.

## 6. Comparison Against PCG Paper Expectations

This audit checked the public paper records for:

- `Stationary Syndrome Decoding for Improved PCGs`, IACR ePrint 2025/295: https://eprint.iacr.org/2025/295
- `Faster Pseudorandom Correlation Generators via Walsh-Hadamard Transform`, IACR ePrint 2026/196: https://eprint.iacr.org/2026/196

The relevant expectation is qualitative here: comparable PCG papers report improvements against optimized PCG baselines and complete setup models. A claimed `1000x` communication win over a comparable baseline would be surprising and would need very strong evidence. Phase 6G does not provide that evidence because it compares:

1. an incomplete RM-VOLE setup-plus-expand harness that excludes split-input product generation, against
2. libOTe noisy subfield VOLE, a deliberately generic and communication-heavy primitive whose chosen `array<u8,m>` instantiation has `O(m^2 N)` payload.

So Phase 6G is not in conflict with the PCG-paper expectation. It is simply not measuring the same object.

## 7. Recommended Paper Wording For Phase 6G

Use conservative language such as:

> We include a preliminary external TCP loopback baseline against libOTe's generic noisy subfield VOLE API. This baseline uses `NoisyVole<array<u8,m>,u8,CoeffCtxArray<u8,m>>` and is not a same-field or SOTA silent-VOLE comparison. Our RM-VOLE numbers in this table measure networked vector-PPRF setup plus local expansion, excluding the secure split-input product generation needed for `Delta*s` and `Delta*e`. These results are therefore diagnostic and should not be interpreted as final end-to-end RM-VOLE LAN performance.

Avoid:

- "RM-VOLE is 1000x lower communication than libOTe" as a headline.
- Main-table comparison against SOTA PCG baselines using the Phase 6G numbers.
- Claims of malicious security.
- Claims that the fields are identical.

Reasonable placement:

- appendix or engineering-evaluation subsection;
- labeled "generic noisy subfield VOLE stress baseline";
- accompanied by the `O(m^2 N)` explanation and missing-cost caveat.

## 8. Recommended Phase 6I

Phase 6I should make the comparison fairer before any paper-ready headline:

1. **Silent subfield VOLE baseline.**
   Implement and measure libOTe `SilentVole<F,G,Ctx>` for matching or clearly documented fields. This is the more appropriate libOTe baseline than noisy VOLE.

2. **Normalization cleanup.**
   Add columns that separate:
   - repetitions;
   - generated `F` elements;
   - byte coordinates;
   - output bits;
   - RM `Fp64` coordinates;
   - one-repetition communication.

3. **Same-field or bit-normalized comparison.**
   Either instantiate a closer same-field baseline, or explicitly compare per output bit/byte and avoid calling `m` the same thing across both methods.

4. **Secure split-input RM-VOLE setup.**
   Implement or model the protocol that produces shares of `Delta*s_i` and `Delta*e_i` when `P0` owns `s,e` and `P1` owns `Delta`. Include reverse VOLE/base-VOLE/multiplication costs.

5. **Updated communication model.**
   Report:
   - vector PPRF setup bytes;
   - split-input product-generation bytes;
   - harness metadata, if any, separately;
   - local expansion time;
   - total end-to-end semi-honest setup-plus-expand time.

## Current Interpretation Of 6G

Valid:

- The libOTe noisy `array<u8,m>` TCP baseline really was run over TCP and really has `O(m^2 N)` communication in this harness.
- The RM-VOLE vector PPRF setup really used TCP/coproto sockets, `DefaultBaseOT`, and `RegularPprf`.
- The RM-VOLE local verify path checks the target coordinate relation after expansion.
- Phase 6G shows that the current vector-PPRF setup subroutine has much lower measured communication than the generic noisy VOLE baseline.

Misleading if used as a headline:

- The `100x` to `3000x` communication ratios are dominated by the noisy baseline's `O(m^2 N)` payload and by omitted RM-VOLE split-input setup costs.
- `entries=m*N` means byte coordinates for libOTe but `Fp64` coordinates for RM-VOLE.
- CSV communication is cumulative over three repetitions while entries are not multiplied by three.
- The fields are different.

Corrected interpretation:

> Phase 6G is a useful diagnostic showing that vector-valued regular PPRF setup can be much cheaper than generic noisy subfield VOLE in this benchmark harness. It is not yet a final apples-to-apples comparison with libOTe silent VOLE or with full secure RM-VOLE setup.
