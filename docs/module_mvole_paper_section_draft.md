# Draft Section: Extension-Field Scalar Ring-sVOLE from QA-SD/WHT

## Motivation and Target Functionality

Many protocols require VOLE-style correlations over a ring while carrying coefficients in an extension field. Running `m` independent scalar QA-SD VOLE instances gives `m` independent right vectors:

```text
for h in [0,m):  z_h = Delta_h * x_h + share_h.
```

This is more general than necessary when the application only needs one base-ring right input multiplied by an extension-field scalar. We therefore reinterpret the construction as an extension-field scalar ring-sVOLE. Let `R_p` be the base ring over `F_p`, and let `R_{p^m}` be the corresponding ring over `F_{p^m}`. The target functionality is:

```text
P0 receives x in R_p and Z0 in R_{p^m}.
P1 receives Delta in F_{p^m} and Z1 in R_{p^m}.
```

The intended correlation is:

```text
Z0 + Z1 = Delta * x.
```

Here `x` has base-field coefficients, so multiplication by `x` scales the extension-field scalar `Delta` by elements of `F_p` at each ring coordinate. After choosing an `F_p`-basis of `F_{p^m}`, this extension-field relation maps to the matrix-shaped relation already tested by the prototype.

Let `phi : R_p -> F_p^N` denote the WHT/evaluation-coordinate map for the base ring, let `psi : F_{p^m} -> F_p^m` denote the coordinate map for the extension-field scalar, and let `Psi : R_{p^m} -> F_p^{m x N}` denote the coordinate/evaluation map for the extension ring. The tested relation is:

```text
Psi(phi(Z0 + Z1)) = psi(Delta) * phi(x)^T.
```

Equivalently, if `psi(Delta) = (Delta_0, ..., Delta_{m-1})`, then for every extension coordinate `h` and ring coordinate `j`,

```text
Psi(Z0)[h,j] + Psi(Z1)[h,j] = Delta_h * phi(x)[j].
```

This functionality is not a drop-in replacement for `m` independent VOLEs, since it deliberately reuses one base-ring right input `x`. The comparison to independent QA-SD VOLEs should therefore be read as a cost comparison for producing the coordinate image of one extension-field scalar ring-sVOLE, not for replacing arbitrary independent VOLE rows.

## Construction Overview

The prototype follows the QA-SD/WHT structure already present in the artifact, but represents the extension field `F_{p^m}` by `m` base-field coordinates. Let `rho` be a public base-ring element and let `s,e` be sparse vectors over the base ring. The prototype forms:

```text
b = rho * s + e
x = WHT(b).
```

For each extension coordinate `h`, it constructs additive shares of `Delta_h * s` and `Delta_h * e`, combines them through the same ring/WHT layer, and outputs coordinate shares of `Z0` and `Z1`. Since `x` is a base-ring element, no general multiplication in `F_{p^m}` is needed in the current test: base-field ring coordinates scale each coordinate of `Delta`. The optimized local implementation uses the WHT-domain identity:

```text
WHT(rho * row + mask) = WHT(rho) * WHT(row) + WHT(mask),
```

avoiding dense row-wise XOR-convolution during expansion.

At present, this is an algebraic correctness and benchmarking prototype. It does not yet implement the final PPRF/silent setup, communication, malicious security, or a complete PCG.

## Correctness

The implementation checks the coordinate image of the extension-field ring-sVOLE relation:

```text
for h in [0,m), j in [0,N):
    Z0[h,j] + Z1[h,j] == Delta[h] * x[j].
```

This is exactly the mapped relation:

```text
Psi(phi(Z0 + Z1)) = psi(Delta) * phi(x)^T.
```

Correctness follows from linearity of the WHT and distributivity of multiplication by the base-field coordinate `phi(x)[j]`. The sparse base relation is:

```text
x = WHT(rho * s + e).
```

The row shares are constructed so that, before WHT, their sum is:

```text
Delta_h * (rho * s + e).
```

Applying WHT row-wise gives:

```text
Z0[h,*] + Z1[h,*]
  = WHT(Delta_h * (rho * s + e))
  = Delta_h * WHT(rho * s + e)
  = Delta_h * phi(x).
```

The implementation verifies this relation for both 64-bit and 32-bit prime-field contexts in the small correctness presets.

## Cost Comparison

Let `N` be the vector length, `m` the number of module rows, `t` the number of sparse/PPRF points, `D = N/t` the PPRF domain per tree, and `L = log2(D)`.

| Model | PPRF setup/path | PPRF evaluation | WHT/local algebra | Communication/key-size intuition |
| --- | --- | --- | --- | --- |
| `m` independent QA-SD VOLEs | Repeats scalar setup `m` times, roughly `O(m t L)`. | Roughly `O(mN)`. | Roughly `O(mN log N)` plus wrapper arithmetic. | Repeats scalar correction/key material and protocol scaffolding `m` times. |
| Extension-field ring-sVOLE algebra/WHT only | Not included. | Not included. | Measured coordinate layer: row-wise WHT and `O(mN)` field operations after optimization. | No communication measured. |
| Ring-sVOLE + independent scalar PPRF adapter | Still repeats scalar PPRF work `m` times, roughly `O(m t L)`. | Roughly `O(mN)`. | Same measured coordinate layer. | Upper-bound baseline; PPRF material still scales with `m`. |
| Ideal ring-sVOLE + shared-path vector PPRF | Target: shared path/setup work `O(t L)` plus vector correction payloads. | Still `O(mN)` leaf payload/output. | Same measured coordinate layer. | Path/base material paid once; correction payload scales with `m`. |

The central win condition is the last row: a vector-valued PPRF should share tree paths, base choices, and internal correction structure across the `m` coordinates, while only the leaf payloads and punctured correction values scale with `m`.

## Implemented and Measured Components

The current prototype includes a standalone coordinate-level algebra/WHT harness and benchmark mode. It measures local Gen, P0 expansion, P1 expansion, verification, and throughput. These timings exclude PPRF generation, silent setup, communication, and malicious checks.

Selected Fp64 measurements:

| `N` | `t` | `m` | Output elems | Algebra/WHT no verify s | Algebra/WHT with verify s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16384 | 32 | 8 | 131072 | 0.014300 | 0.014874 |
| 16384 | 32 | 16 | 262144 | 0.025202 | 0.026352 |
| 16384 | 32 | 64 | 1048576 | 0.093543 | 0.098095 |
| 65536 | 64 | 16 | 1048576 | 0.110740 | 0.115298 |
| 65536 | 64 | 32 | 2097152 | 0.206327 | 0.215452 |
| 262144 | 64 | 16 | 4194304 | 0.496800 | 0.515040 |

The optimized local layer sustains roughly `7.3M` to `11.2M` output field elements per second without verification over the measured grid.

The original scalar QA-SD VOLE benchmark was also run at nearby sizes:

| Command | Fp64 time s | Fp32 time s |
| --- | ---: | ---: |
| `./build/main --QA_VOLE 14` | 0.0162073 | 0.0149026 |
| `./build/main --QA_VOLE 16` | 0.0235897 | 0.0194725 |
| `./build/main --QA_VOLE 18` | 0.0575277 | 0.0428349 |

Using the Fp64 numbers, a naive estimate for `m` independent QA-SD VOLEs is:

```text
T_independent(N,m) = m * T_QA_VOLE_Fp64(N).
```

The PPRF adapter test currently uses `m` independent scalar `RegularPprf` calls with domain-separated seeds. For `N=1024`, `t=8`, `m=8`, it measured:

```text
adapter_total_s = 0.000192
adapter_throughput = 42.7M field elements/sec.
```

This is a small isolated correctness/API baseline. Extrapolating it to larger `N,m` is useful only as a rough upper-bound estimate, not as an end-to-end protocol claim.

One conservative combined estimate is:

```text
T_current_adapter(N,m)
  ~= T_algebra_no_verify(N,m) + (mN / 42.7M).
```

Under this estimate, the current coordinate-level ring-sVOLE prototype with independent scalar PPRFs remains below the naive `m * QA_VOLE` estimate across the measured grid, but the comparison is imperfect because the PPRF adapter does not include the full silent setup or integration overhead.

## Remaining Work

The most important missing component is a shared-path vector-valued PPRF. The existing `RegularPprf` API is scalar-output: leaf expansion writes one field element per leaf, and correction serialization is sized for one `F`. It can support a vector-valued baseline by running `m` independent scalar PPRFs, but it does not directly expose the desired shared-path vector output.

An optimized vector-valued PPRF should:

1. Expand one PPRF tree/path structure per punctured point.
2. Derive `m` field elements per leaf using domain-separated hashing.
3. Serialize vector-valued punctured corrections.
4. Output either `m` scalar planes or a packed `mN` field-element buffer.
5. Preserve the existing scalar PPRF behavior for the original QA-SD VOLE code.

Only after this shared-path adapter is correct should it be integrated into the extension-field ring-sVOLE Gen/Expand structure.

## Conservative Claim

The evidence so far supports the following conservative claim: the local coordinate algebra/WHT layer for extension-field scalar ring-sVOLE is efficient and is unlikely to be the dominant cost at the tested sizes. This construction can plausibly outperform `m` independent QA-SD VOLE instances when the application needs the relation `Z0 + Z1 = Delta * x` with `x in R_p` and `Delta in F_{p^m}`, especially if the PPRF layer is upgraded to share tree/path work across the `m` extension-field coordinates.

The current code is not yet a complete end-to-end PCG for this extension-field scalar ring-sVOLE. It is a correctness prototype plus local benchmarks and an isolated independent-scalar-PPRF adapter test. The main remaining implementation step is the shared-path vector-valued PPRF.
