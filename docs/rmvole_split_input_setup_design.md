# RM-VOLE Split-Input Setup Design

Phase 6J designs the missing secure setup layer for RM-VOLE PPRF payloads. No protocol code was changed.

## Bottom Line

Yes, the current RM-VOLE network benchmark is missing an essential setup cost.

The Phase 6F/6G harness centrally computes:

```text
beta_i = Delta * s_i
eta_i  = Delta * e_i
```

and feeds those full payloads into vector-valued `RegularPprf`. That is correct for API/correctness testing, but it is not a secure two-party setup because:

- `P0` should know sparse positions and values `s_i,e_i in F_p`;
- `P1` should know `Delta in F_{p^m}`;
- neither party should learn the other party's input beyond allowed leakage;
- the current `RegularPprfSender::expand(..., beta, ...)` API expects the sender to know the programmed payload.

The minimal semi-honest fix is to add a split-input product-sharing layer for the `2t` sparse coefficients and to reverse the PPRF roles:

- `P0` is the `RegularPprfReceiver`, because `P0` owns the puncture positions.
- `P1` is the `RegularPprfSender`, because `P1` owns `Delta` and can program its product-share payloads.
- A subfield-VOLE/OLE layer gives additive shares of `Delta*q_i`, where `q_i` ranges over all `s_i` and `e_i`.

## Ideal Functionality

Public parameters:

```text
N = 2^logN
t = number of regular sparse blocks
blockSize = N/t
m = number of F_p coordinates representing Delta
```

Inputs:

```text
P0:
  for each sparse vector kind K in {s,e}
  offsets alpha_K[i] in [0, blockSize)
  values q_K[i] in F_p, one per block i

P1:
  Delta = (Delta_0, ..., Delta_{m-1}) in F_p^m
```

Output:

For each `K in {s,e}` and block `i`, parties receive vector shares:

```text
P0: A_K[i] in F_p^m
P1: B_K[i] in F_p^m
A_K[i] + B_K[i] = Delta * q_K[i]
```

The shares are then injected into a regular-block PPRF so that the expanded vectors reconstruct:

```text
share0_K[h][j] + share1_K[h][j] =
  Delta_h * q_K[i]  if j = i*blockSize + alpha_K[i]
  0                 otherwise
```

Allowed leakage in the semi-honest target:

- public `N,t,m,blockSize`;
- no support offsets or sparse values to `P1`;
- no `Delta` to `P0`;
- no full products `Delta*q_i` to either party.

## Correct PPRF Role Assignment

Current centralized harness uses one side to know full `beta` and program `RegularPprfSender`.

For secure split-input setup, use this role assignment instead:

```text
P0 = RegularPprfReceiver, sets choice bits from sparse offsets.
P1 = RegularPprfSender, programs a masked payload share.
```

Suppose a split-input VOLE gives:

```text
P0 receives a_i
P1 receives b_i
a_i = b_i + Delta*q_i
```

Then P1 programs the PPRF point payload:

```text
y_i = -b_i
```

At the selected leaf, `RegularPprf` gives:

```text
receiverOut = senderOut + y_i
```

Define final expanded shares:

```text
P0 share at selected leaf = receiverOut + a_i
P1 share at selected leaf = -senderOut
```

Then:

```text
(receiverOut + a_i) + (-senderOut)
  = y_i + a_i
  = -b_i + a_i
  = Delta*q_i
```

At non-selected leaves, `receiverOut = senderOut`, so:

```text
receiverOut + (-senderOut) = 0
```

This is the same structural idea used in silent VOLE setup code: a small VOLE produces base product shares, the sender programs one share into PPRF, and the receiver adds its share at the selected points.

## Existing Code Patterns

### WHTPCG / SilentWalsh

`SilentWalshVoleSender.h` and `SilentWalshVoleReceiver.h` show the standard structure:

- receiver samples sparse/noise values;
- a small noisy VOLE produces shares of `Delta * noiseVals`;
- `RegularPprf` expands those sparse correlations;
- receiver adds its base share at selected positions;
- sender programs the negated/masked base share.

The comments in `SilentWalshVoleReceiver::genSilentBaseOts()` explicitly describe computing shares of `delta * noiseVals` and then using PPRF to place them at sparse coordinates.

This is not a drop-in RM-VOLE setup because SilentWalsh samples its own noise structure and then applies its WHT-style code. But it is the right design template for the split-input layer.

### Current RM-VOLE PPRF Harness

`RmvolePprfNetSetupTest.h` and `RmvoleNetBench.h` already implement:

- vector-valued `RegularPprf` over fixed prime coordinate arrays;
- regular-block shape `domainSize = blockSize`, `pointCount = t`;
- vector mode with one PPRF for `s` and one for `e`;
- local verification of `share0 + share1 = Delta*q` at selected points.

What is missing is how to obtain the programmed payload shares without centralized knowledge of both `Delta` and `q`.

## Is This Reverse VOLE, Subfield VOLE, Or Scalar Multiplication Sharing?

The needed primitive is a chosen-input subfield VOLE/OLE:

```text
P0 input: q_i in F_p
P1 input: Delta in F_p^m
P0 output: a_i
P1 output: b_i
a_i = b_i + Delta*q_i
```

Depending on convention, it may be called "reverse" relative to the desired PPRF direction because:

- the party with PPRF choices/supports (`P0`) is the VOLE receiver with `c=q_i`;
- the party with `Delta` (`P1`) is the VOLE sender;
- the PPRF sender (`P1`) programs `-b_i`, not the full product.

Mathematically, this is exactly subfield VOLE for `2t` coefficients, or equivalently batched scalar multiplication sharing over the base field coordinates.

## Candidate Protocols

### A. Use libOTe Subfield VOLE For 2t Products

Run one chosen-input subfield VOLE of length `2t`:

```text
c = (s_0,...,s_{t-1}, e_0,...,e_{t-1})
Delta = vector in F_p^m
```

Outputs:

```text
P0: a_i in F_p^m
P1: b_i in F_p^m
a_i = b_i + Delta*c_i
```

Then:

- split the first `t` outputs for `s`;
- split the next `t` outputs for `e`;
- P1 programs vector PPRF payloads `-b_i`;
- P0 adds `a_i` at selected positions after PPRF expansion.

Pros:

- Conceptually exact.
- Directly matches the libOTe relation already audited for noisy VOLE.
- Batches all `2t` products.

Cons:

- The available generic `NoisyVole<array<u8,m>,u8>` baseline is not same-field with RM-VOLE.
- A generic vector/array noisy VOLE decomposes the whole vector `Delta`, causing `O(m^2 t)` setup communication for the split layer.
- Silent subfield VOLE for `array<u8,m>,u8` did not compile in Phase 6I.
- A same-field `Fp64` vector subfield VOLE would require a proper context and careful normalization.

Estimated communication with generic noisy vector VOLE:

```text
L = 2t products
bitSize(Delta vector) ~= 64m
byteSize(vector element) ~= 8m
dominant bytes ~= L * 64m * 8m = 1024 m^2 t
```

This may still be acceptable for small `t`, but it is not the cleanest match.

### B. Use m Scalar VOLEs Over Fp

Run `m` scalar OLE/VOLE instances, each of length `2t`.

For coordinate `h`:

```text
P0 input: c_i = s_i/e_i in F_p
P1 input: Delta_h in F_p
outputs: a_{i,h} = b_{i,h} + Delta_h*c_i
```

Pack the `m` scalar outputs into vector shares:

```text
a_i = (a_{i,0},...,a_{i,m-1})
b_i = (b_{i,0},...,b_{i,m-1})
```

Pros:

- Matches the current RM-VOLE coordinate semantics exactly.
- Avoids the `O(m^2 t)` vector noisy-VOLE cost.
- Batches over all `2t` sparse coefficients per coordinate.
- Can be implemented with existing `CoeffCtxIntegerPrime_64` style arithmetic.

Cons:

- Requires a scalar prime-field OLE/VOLE implementation over `Fp64`.
- libOTe's generic noisy VOLE can instantiate scalar integer-like contexts, but using it for `Fp64` should be smoke-tested carefully because its binary decomposition is over the machine representation and arithmetic must remain modulo `p`.
- Repeating base OTs per coordinate would be wasteful unless batched/reused.

Estimated noisy scalar communication:

```text
m coordinates
L = 2t products per coordinate
bitSize(Delta_h) ~= 64
byteSize(Fp64) = 8
dominant bytes ~= m * L * 64 * 8 = 1024 m t
```

This is the most direct minimal semi-honest path if a scalar prime OLE is used.

### C. OT-Based Scalar Multiplication Sharing

Implement scalar multiplication sharing directly using OT extension.

For each product `Delta_h * q_i`, decompose `q_i` into bits:

```text
q_i = sum_l q_{i,l} 2^l
```

For each bit `l`, P1 samples a random mask `r_{i,h,l}` and offers:

```text
message 0: r_{i,h,l}
message 1: r_{i,h,l} + 2^l * Delta_h
```

P0 receives according to bit `q_{i,l}`. Summing received values gives P0's share. P1's share is the negative sum of the masks.

Pros:

- Simple, explicit, same-field over `Fp64`.
- Easy to batch all products as OT extension calls.
- Does not need libOTe generic field contexts beyond OT.
- Security intuition is straightforward in semi-honest mode.

Cons:

- Communication is roughly one OT payload per bit per product coordinate.
- Needs careful implementation to use OT extension, not base OT per bit.
- Needs modular reduction and message packing.

Estimated communication:

```text
products = 2t*m
bits per scalar = about 61 or 64
payload per OT message = one Fp64 element
dominant payload roughly O(2t*m*64*8)
```

This is essentially a hand-rolled scalar OLE and should be treated as such.

### D. Reuse WHTPCG / SilentWalsh Setup Structure

Adapt the SilentWalsh pattern:

1. Generate small product shares with noisy VOLE.
2. Use PPRF to place those shares at hidden sparse positions.
3. Apply WHT/local expansion.

Pros:

- This is the closest existing implementation pattern.
- It already uses noisy VOLE to get base product shares and PPRF to place sparse correlations.
- It clarifies role assignment and share signs.

Cons:

- Existing `SilentWalshVoleSender/Receiver` generate their own random noise values and use their own WHT encoding path.
- They are not structured for chosen regular-block sparse `s,e` with externally supplied `Delta`.
- Direct reuse risks mixing the QA/WHT backend with the isolated RM-VOLE benchmark, which this phase explicitly avoids.

Recommended use:

- Reuse the design pattern and sign conventions.
- Do not call or modify SilentWalsh code for Phase 6K.

## Does Vector-Valued PPRF Remove The Split-Input Cost?

No.

Vector-valued PPRF removes repeated tree/path/base-OT setup across the `m` coordinates. It efficiently carries an `m`-coordinate payload once the payload share is known.

It does not compute:

```text
Delta_h * s_i
Delta_h * e_i
```

from split inputs. That product-sharing cost is separate and must be included in any honest end-to-end setup communication.

## Leakage And Security Notes

### Support Positions

If `P0` owns sparse positions, `P0` should be the `RegularPprfReceiver`.

`RegularPprfReceiver` encodes support offsets as OT choice bits. In the semi-honest setting, this hides offsets from `P1` up to standard OT/PPRF security. Public leakage remains:

- `N`;
- `t`;
- `blockSize`;
- one puncture per block for each of `s` and `e`;
- message sizes and timing.

The current centralized harness sends sparse offsets as metadata and therefore leaks them to the other role. Phase 6K should remove that metadata from bench mode.

### Sparse Values

The split-input VOLE/OLE hides `s_i,e_i` from `P1` under receiver privacy. `P1` only sees its random product shares.

### Delta

The split-input VOLE/OLE hides `Delta` from `P0` because P0 receives randomized shares `a_i`, not the full products.

### Malicious Security

This design is semi-honest only. A malicious-secure extension would need:

- malicious OT/VOLE or consistency checks for the scalar multiplication layer;
- checks that P0 uses one valid support per regular block;
- checks that P0 uses the same `s/e` values consistently across all `m` coordinate products;
- checks that P1 uses the same `Delta_h` across all `2t` products;
- PPRF consistency/malicious checks;
- possibly MACs or sacrifice checks over the generated correlations;
- domain separation and transcript binding between split-input setup and PPRF programming.

## Minimal Semi-Honest Phase 6K Plan

Recommended implementation path:

1. **Implement split product-share API.**
   Add an isolated helper that takes:
   ```text
   P0: q[0..2t-1] in Fp64
   P1: Delta[0..m-1] in Fp64
   ```
   and returns:
   ```text
   P0: A[2t][m]
   P1: B[2t][m]
   A[k][h] = B[k][h] + Delta[h] * q[k]
   ```

2. **Start with candidate C or B.**
   The simplest robust path is OT-based scalar multiplication sharing or `m` scalar noisy VOLEs over `Fp64`, because it matches current coordinate semantics and avoids vector noisy VOLE's `O(m^2 t)` cost.

3. **Flip PPRF roles.**
   Use:
   ```text
   P0 = RegularPprfReceiver, choices from sparse offsets
   P1 = RegularPprfSender, payloads = -B[k]
   ```

4. **Inject P0's product share locally.**
   After PPRF expansion, P0 adds `A[k]` at its selected leaf. P1 stores `-senderOut` as its expanded share.

5. **Keep two PPRFs.**
   Run one vector PPRF for `s` and one for `e`, preserving the existing regular-block shape:
   ```text
   domainSize = blockSize
   pointCount = t
   ```

6. **Measure separate components.**
   Report:
   - split-input product setup time/bytes;
   - vector PPRF setup time/bytes;
   - local WHT expansion time;
   - total semi-honest setup-plus-expand.

7. **Verify without benchmark leakage.**
   Keep `verify` mode opening outputs after timing. Bench mode should not send `Delta`, sparse offsets, sparse values, or product openings as harness metadata.

## Estimated Communication Summary

Let `L = 2t` product coefficients.

| Candidate | Dominant split-input communication | Notes |
|---|---:|---|
| A. vector subfield VOLE | `~1024 m^2 t` bytes for noisy 64-bit vector decomposition | conceptually exact but repeats Phase 6G quadratic-in-m issue on small `2t` |
| B. m scalar VOLEs | `~1024 m t` bytes for noisy 64-bit scalar decomposition | best match to current coordinate semantics |
| C. OT scalar multiplication | `O(64 * 2t * m * 8)` payload bytes plus OT-extension overhead | simplest explicit implementation |
| D. SilentWalsh pattern | depends on base VOLE + PPRF + code | design template, not drop-in |

These estimates are for the missing split-input layer only. Total Phase 6K communication should add the existing vector-PPRF setup bytes and any OT-extension/base-OT setup costs.

## Interpretation Impact For Phase 6G

Phase 6G remains useful as a preliminary engineering comparison, but its RM-VOLE line omits this split-input layer. Therefore:

- the current RM-VOLE network benchmark is not a complete protocol runtime;
- the communication reductions against noisy subfield VOLE are upper bounds for the current incomplete setup;
- adding split-input setup will increase RM-VOLE bytes and runtime;
- the increase should scale with `t` and `m`, not with `N`, if implemented as a sparse coefficient product-sharing layer.

This does not invalidate the vector PPRF result. It clarifies that vector PPRF carries sparse payload shares efficiently, while another primitive must produce those shares securely.

## Recommended Wording

Until Phase 6K is implemented:

> Our RM-VOLE network benchmark measures vector-PPRF setup plus local expansion with centralized payload generation. A complete semi-honest protocol also needs a split-input product-sharing layer for `Delta*s_i` and `Delta*e_i`. We design that layer separately and do not claim full end-to-end RM-VOLE communication until it is included.

