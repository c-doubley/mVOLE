# Local Benchmark Engineering Report

The target relation is `Z0[i] + Z1[i] = x[i] * Delta` with `x` in `F_p^N` and `Delta` mathematically in `F_{p^m}`. `ExtElem` is a fixed-basis coordinate representation of one element of `F_{p^m}`; no general extension-extension multiplication is implemented or required.

RM_VECTOR is PCG-based: setup uses length-`2t` direct noisy subfield VOLE for split-input product sharing and two vector-valued RegularPprf executions. Expand is local and silent after setup. RM_COORD is the same-functionality coordinate-wise ablation target, requiring `2m` scalar PPRFs; the integrated end-to-end harness is absent here and is reported as skipped.

The direct noisy subfield VOLE relation in libOTe is `a = b + c * Delta`; the RM-VOLE mapping is `x=c`, `Z0=a`, `Z1=-b`. Measured fixed-prime direct rows were not produced by this run; resource preflight/model rows are separated from measured rows.

Measured RM_VECTOR end-to-end rows use two local processes over TCP loopback, not LAN. Component PPRF rows use the existing local async setup harness. The base-OT convention is `INCLUDING_BASE_OT`; both base-OT modes were not implemented uniformly.

The checked-in artifact bundle was generated as an engineering grid with one measured repetition after one warm-up per measured point. It is useful for local validation and reproduction plumbing, but it is not the full 10-repetition final campaign requested for paper-ready statistics.

No state-of-the-art superiority claim is made. Silent sVOLE remains future work, and the legacy u8 subfield baseline is not a same-field comparison.
