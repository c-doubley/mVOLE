# ModuleMVOLE Cost Analysis

## Question

Can ModuleMVOLE be faster than running `m` independent QA-SD VOLE instances?

Short answer: yes, but only for the right functionality and only after the PPRF layer is made vector-valued. The current optimized algebra/WHT layer is already fast enough that it is unlikely to be the bottleneck. The remaining question is whether the PPRF/silent setup can avoid repeating the scalar QA-SD path `m` times.

## Structural Difference

The two outputs are not identical distributions.

`m` independent QA-SD VOLE instances produce `m` independent scalar VOLE vectors:

```text
for h in [0,m):  z_h = Delta_h * x_h + share_h
```

Each row has its own right vector `x_h`.

ModuleMVOLE produces a rank-1 matrix-style correlation:

```text
P0: x in F_p^N, Z0 in F_p^{m x N}
P1: Delta in F_p^m, Z1 in F_p^{m x N}
Z0 + Z1 = Delta * x^T
```

There is one shared right vector `x` and `m` left coefficients `Delta_h`. Therefore the comparison is about cost per `mN` field correlations when the application naturally wants shared-right-vector Matrix-VOLE, not about replacing arbitrary independent VOLE rows.

## Measured Inputs

### ModuleMVOLE algebra/WHT layer

These timings are from `docs/module_mvole_benchmark.md`. They do not include PPRF, silent setup, communication, or malicious checks.

| `N` | `t` | `m` | Output elems | Gen s | Expand P0 s | Expand P1 s | Verify s | Total no verify s | Total with verify s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16384 | 32 | 8 | 131072 | 0.007069 | 0.003673 | 0.003558 | 0.000575 | 0.014300 | 0.014874 |
| 16384 | 32 | 16 | 262144 | 0.011065 | 0.007141 | 0.006996 | 0.001149 | 0.025202 | 0.026352 |
| 16384 | 32 | 32 | 524288 | 0.019344 | 0.014077 | 0.013913 | 0.002288 | 0.047334 | 0.049623 |
| 16384 | 32 | 64 | 1048576 | 0.037429 | 0.027976 | 0.028138 | 0.004552 | 0.093543 | 0.098095 |
| 65536 | 64 | 8 | 524288 | 0.034613 | 0.015325 | 0.014843 | 0.002282 | 0.064780 | 0.067063 |
| 65536 | 64 | 16 | 1048576 | 0.051733 | 0.029698 | 0.029309 | 0.004559 | 0.110740 | 0.115298 |
| 65536 | 64 | 32 | 2097152 | 0.088302 | 0.059376 | 0.058649 | 0.009126 | 0.206327 | 0.215452 |
| 262144 | 64 | 8 | 2097152 | 0.140775 | 0.075154 | 0.072352 | 0.009158 | 0.288281 | 0.297440 |
| 262144 | 64 | 16 | 4194304 | 0.209202 | 0.145164 | 0.142434 | 0.018240 | 0.496800 | 0.515040 |

### PPRF adapter test

From `docs/module_mvole_pprf.md`:

```text
MODULE_PPRF_TEST Fp64 N=1024 t=8 m=8 domain=128 output_elems=8192
scalar_setup_s=0.000005 scalar_expand_s=0.000077 scalar_total_s=0.000082
adapter_setup_s=0.000007 adapter_expand_s=0.000185 adapter_total_s=0.000192
adapter_throughput_elems_per_s=42713580.318785 PASS
```

The adapter currently runs `m` independent scalar `RegularPprf` executions with domain-separated seeds. It is a correctness/API baseline, not the optimized shared-path design.

### Original QA-SD VOLE timing

From `docs/module_mvole_benchmark.md`:

```text
COMMAND ./build/main --QA_VOLE 14
The total time of VOLE based on QA-SD code consume 0.0162073 seconds
The total time of VOLE based on QA-SD code consume 0.0149026 seconds
COMMAND ./build/main --QA_VOLE 16
The total time of VOLE based on QA-SD code consume 0.0235897 seconds
The total time of VOLE based on QA-SD code consume 0.0194725 seconds
COMMAND ./build/main --QA_VOLE 18
The total time of VOLE based on QA-SD code consume 0.0575277 seconds
The total time of VOLE based on QA-SD code consume 0.0428349 seconds
```

The first line at each `N` is Fp64 and the second is Fp32. The estimates below use Fp64.

## Cost Models

Let:

```text
N = vector length
m = number of module rows
t = sparse/PPRF point count
D = N / t = PPRF domain per tree
L = log2(D)
```

| Model | PPRF setup/path | PPRF full evaluation | WHT | Field ops | Communication/key-size intuition |
| --- | --- | --- | --- | --- | --- |
| A. `m` independent QA-SD VOLEs | Repeats scalar QA-SD setup `m` times. Roughly `O(m t L)` path/base material, with constants from two silent VOLE calls for `s` and `e`. | Roughly `O(m N)` scalar PPRF leaf expansion. | Roughly `O(m N log N)` across independent scalar rows. | Roughly `O(mN)` wrapper arithmetic. | Repeats scalar PPRF correction/key material `m` times. Communication scales like `m` scalar protocols. |
| B. ModuleMVOLE algebra/WHT only | None. | None. | `O(m N log N)` row WHT plus one shared-vector WHT component. | `O(mN)` row multiplications/additions after WHT-domain optimization. | No protocol communication measured. Local-only layer. |
| C. ModuleMVOLE + current independent scalar PPRF adapter | Still roughly `O(m t L)` because it runs `m` scalar PPRFs. | Roughly `O(m N)` scalar leaf expansion. | Same as Model B. | Same as Model B plus PPRF correction arithmetic. | Better algebra reuse than `m` full QA-SD wrappers, but PPRF correction/key material still scales with `m`. This is an upper-bound baseline for the adapter design. |
| D. Ideal ModuleMVOLE + shared-path vector PPRF | Target is `O(t L)` shared tree/path work plus vector correction payloads. | `O(m N)` leaf material is still needed to output `mN` field elements. | Same as Model B. | Same as Model B plus vector PPRF correction arithmetic. | Path/base material paid once per sparse point. Correction payload scales with `m`, but tree traversal and base/path keys are shared. |

## Conservative Time Estimates

For Model A, estimate `m` independent QA-SD VOLEs as:

```text
T_A(N,m) = m * T_QA_VOLE_Fp64(N)
```

For Model C, the only measured PPRF adapter point is `N=1024, t=8, m=8`. A conservative placeholder estimate from that microbenchmark is:

```text
PPRF_adapter_rate = 42.7M field elements/sec
T_PPRF_adapter(N,m) ~= (mN) / 42.7M
T_C(N,m) ~= T_Module_algebra_no_verify(N,m) + T_PPRF_adapter(N,m)
```

This is an extrapolation from an isolated local PPRF test with synthetic local base material. It is useful for ranking the local layers, not for claiming end-to-end protocol timing.

| `N` | `m` | `mN` | A: `m * QA_VOLE_Fp64` s | B: algebra/WHT no verify s | C: B + extrapolated independent PPRF adapter s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16384 | 8 | 131072 | 0.129658 | 0.014300 | 0.017369 |
| 16384 | 16 | 262144 | 0.259317 | 0.025202 | 0.031340 |
| 16384 | 32 | 524288 | 0.518634 | 0.047334 | 0.059610 |
| 16384 | 64 | 1048576 | 1.037267 | 0.093543 | 0.118097 |
| 65536 | 8 | 524288 | 0.188718 | 0.064780 | 0.077056 |
| 65536 | 16 | 1048576 | 0.377435 | 0.110740 | 0.135294 |
| 65536 | 32 | 2097152 | 0.754870 | 0.206327 | 0.255435 |
| 262144 | 8 | 2097152 | 0.460222 | 0.288281 | 0.337389 |
| 262144 | 16 | 4194304 | 0.920443 | 0.496800 | 0.595016 |

These estimates suggest that even with independent scalar PPRFs, ModuleMVOLE may be competitive when the target output is the shared-right-vector rank-1 matrix correlation. The margin shrinks as `N` grows because the measured algebra/WHT layer becomes a larger part of the total, but it remains below the naive `m * QA_VOLE` estimate in the measured grid.

The table should not be read as a final performance claim. Model C does not include full silent setup, base OT/noisy VOLE costs, or real integration overhead. The correct next comparison is an integrated prototype with the same setup assumptions on both sides.

## Why Shared-Path PPRF Matters

With independent scalar PPRFs, ModuleMVOLE still repeats the PPRF tree setup and correction path `m` times. This limits the advantage over running `m` scalar protocols.

The ideal vector-valued PPRF would share:

```text
tree paths, base choices, internal correction structure
```

and only scale these parts with `m`:

```text
leaf payloads, punctured correction values, final mN field output
```

That is the win condition. If path/base material is a meaningful fraction of the scalar QA-SD VOLE cost, sharing it once across all `m` rows should make ModuleMVOLE substantially better than `m` independent QA-SD VOLEs.

## Communication Intuition

For `m` independent QA-SD VOLEs, communication and key material scale like `m` full scalar instances. In the current code, each scalar instance eventually configures regular PPRF parameters with `mPntCount = 64` and `mSizePer = N / 64`, then exchanges correction data for that scalar output.

For ideal ModuleMVOLE, the internal PPRF structure should use one set of punctured paths and carry vector-valued corrections. That changes the communication shape from approximately:

```text
m * (path correction bytes + field correction bytes)
```

toward:

```text
path correction bytes + m * field correction bytes
```

The output itself is still `mN` field elements, so leaf/output bandwidth cannot disappear. The plausible saving is in repeated path/setup/control material and repeated full protocol scaffolding.

## Conservative Conclusions

1. ModuleMVOLE is not a drop-in replacement for `m` arbitrary independent QA-SD VOLEs. It is attractive when the application wants `Z = Delta * x^T` with a shared right vector.
2. The optimized local algebra/WHT layer is fast enough that it is no longer the main obstacle.
3. The current independent-scalar-PPRF adapter is useful as an upper-bound baseline and correctness test, but it does not expose the intended asymptotic advantage.
4. A shared-path vector-valued PPRF is the key implementation step for a meaningful paper-quality performance story.
5. If the shared-path PPRF can pay path/setup cost once and only scale leaf payloads with `m`, ModuleMVOLE should beat `m` independent QA-SD VOLEs for shared-right-vector Matrix-VOLE workloads.

## Recommended Next Implementation Step

Build an isolated shared-path vector-valued PPRF experiment before integrating with ModuleMVOLE:

1. Keep the existing scalar `RegularPprf` API untouched.
2. Add a small experimental adapter that expands one PPRF tree structure but derives `m` field elements per leaf via domain-separated hashing.
3. Extend correction serialization to carry `m` field elements at punctured leaves.
4. Test it against the same relation used by `--MODULE_PPRF_TEST`.
5. Compare it against the current `m` independent scalar adapter at `N=2^14`, `2^16`, and `2^18`.

Only after that shared-path adapter passes should it be connected to the ModuleMVOLE Gen/Expand structure.
