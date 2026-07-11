# RM-VOLE vs libOTe noisy subfield VOLE with real split-input VOLE
This Phase 6M comparison reruns the TCP loopback grid with RM-VOLE using `split-input-vole`: real libOTe noisy VOLE for the `2t` sparse coefficient products, vector-valued `RegularPprf` payload distribution, and local WHT expansion. It is the conservative RM-VOLE network setup comparison currently available in this branch.
The libOTe comparator is still the generic noisy subfield VOLE harness (`--SUBFIELD_VOLE_NET_BENCH`). This is not a final SOTA comparison: RM-VOLE uses noisy VOLE for split-input setup rather than the intended silent/batched product layer, and the libOTe noisy baseline has the previously observed `O(m^2 N)`-like communication in this instantiation.
All runs are one-host TCP loopback with `reps=3`. Aggregate runtime uses `max(server, client)`. RM communication is `clean_protocol_bytes` from the benchmark, excluding verification openings; for `bench` mode there were no verification openings. Subfield VOLE communication uses the total two-role socket bytes reported by the harness.
Raw process outputs are in `docs/phase6m_raw_outputs/` and `docs/phase6m_optional_raw_outputs/`.
## Results
| logN | t | m | RM total s | RM split s | RM comm bytes | libOTe total s | libOTe comm bytes | runtime ratio sub/RM | comm ratio sub/RM |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 12 | 8 | 8 | 0.045208 | 0.034407 | 1664880 | 0.020351 | 6298176 | 0.45x | 3.78x |
| 12 | 8 | 16 | 0.091736 | 0.079008 | 6447984 | 0.085983 | 25179072 | 0.94x | 3.90x |
| 14 | 16 | 8 | 0.066537 | 0.039032 | 3283392 | 0.090162 | 25172544 | 1.36x | 7.67x |
| 14 | 16 | 16 | 0.156831 | 0.111382 | 12797376 | 0.212381 | 100676544 | 1.35x | 7.87x |
| 14 | 16 | 32 | 0.254786 | 0.215176 | 50699712 | 0.513228 | 402679488 | 2.01x | 7.94x |
| 16 | 64 | 8 | 0.212115 | 0.091876 | 12975456 | 0.269339 | 100670016 | 1.27x | 7.76x |
| 16 | 64 | 16 | 0.306458 | 0.184650 | 50874720 | 0.903167 | 402666432 | 2.95x | 7.91x |

## Interpretation
- RM-VOLE still reduces communication versus this generic noisy subfield baseline on every completed point, but the reduction is now conservative and much smaller than the earlier centralized/simulated setup comparison because split-input product sharing is included.
- The RM-VOLE split-input noisy VOLE layer dominates communication: roughly 97% to 99% of clean protocol bytes on these points.
- Runtime is mixed at small sizes: libOTe noisy subfield VOLE is faster for `logN=12,m=8`, near parity at `logN=12,m=16`, and RM-VOLE is faster on the larger completed points.
- The RM-VOLE split-input layer is intentionally conservative here. A silent or otherwise optimized sparse-product setup is still needed before making a final paper claim.
- The fields are not identical: RM-VOLE uses fixed-prime `Fp64` vector coordinates, while the libOTe noisy subfield baseline uses its generic subfield/extension representation. Treat this as a measured external noisy baseline, not an apples-to-apples same-field comparison.

## Commands
RM-VOLE example: `./build/main --RMVOLE_NET_BENCH server 0.0.0.0 <port> <logN> <t> <m> 3 bench split-input-vole` paired with `client 127.0.0.1`.
libOTe noisy subfield example: `./build/main --SUBFIELD_VOLE_NET_BENCH server 0.0.0.0 <port> <logN> <m> 3` paired with `client 127.0.0.1`.
