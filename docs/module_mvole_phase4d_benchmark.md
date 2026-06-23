# Phase 4D RM-VOLE Benchmark Comparison

## Scope

This note compares four measured paths on branch `module-mvole`:

1. RM-VOLE local algebra only: `--MODULE_MVOLE_BENCH`
2. RM-VOLE with the shared-path vector PPRF setup prototype: `--MODULE_MVOLE_PPRF`
3. Isolated shared-path vector PPRF: `--MODULE_VECTOR_PPRF_TEST`
4. Original QA-SD VOLE: `--QA_VOLE k`

It also reports a conservative estimate for `m` independent QA-SD VOLE runs:

```text
T_independent(m,N) = m * T_QA_VOLE(N)
```

The first printed `QA_VOLE` time is the 64-bit prime-field run, and the second is the 32-bit prime-field run, matching the call order in `main.cpp`.

## Caveats

- RM-VOLE produces one shared `x in R_p` and one `Delta in F_{p^m}`. It is not distribution-equivalent to `m` independent QA-SD VOLE instances with independent right vectors.
- The meaningful comparison is for applications that want the rank-1/tensor correlation

```text
Psi(Z) = psi(Delta) * phi(x)^T.
```

- `--MODULE_MVOLE_PPRF` uses a local shared-path vector-valued PPRF prototype. It is not yet an optimized `libOTe`/silent setup integration.
- The isolated vector PPRF test still materializes full local share matrices. The RM-VOLE_PPRF path now streams the vector-PPRF shares directly into the RM-VOLE row-share layout, but it is still a local prototype rather than a final protocol implementation.

## Commands

```bash
git status --short
cmake --build build --parallel 64

./build/main --MODULE_MVOLE_PPRF 1
./build/main --MODULE_MVOLE_PPRF 2
./build/main --MODULE_MVOLE_PPRF 3
./build/main --MODULE_MVOLE_PPRF 4
./build/main --MODULE_MVOLE_PPRF 5
./build/main --MODULE_MVOLE_PPRF 6
./build/main --MODULE_MVOLE_PPRF 7

./build/main --MODULE_MVOLE_BENCH 1
./build/main --MODULE_MVOLE_BENCH 2
./build/main --MODULE_MVOLE_BENCH 3

./build/main --MODULE_VECTOR_PPRF_TEST 1
./build/main --MODULE_VECTOR_PPRF_TEST 2
./build/main --MODULE_VECTOR_PPRF_TEST 3

./build/main --QA_VOLE 10
./build/main --QA_VOLE 12
./build/main --QA_VOLE 14
./build/main --QA_VOLE 16
./build/main --QA_VOLE 18
```

The build completed successfully. All requested `QA_VOLE` runs exited with status 0.

## RM-VOLE With Shared-Path Vector PPRF

| Preset | `N` | `t` | `m` | Block size `N/t` | Expanded leaves / sparse vector | PPRF `s` setup s | PPRF `e` setup s | Expand P0 s | Expand P1 s | Verify s | Total s | Throughput elems/s | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 1024 | 8 | 8 | 128 | 1024 | 0.000098 | 0.000094 | 0.000240 | 0.000197 | 0.000035 | 0.000878 | 9329121.720851 | PASS |
| 2 | 4096 | 16 | 16 | 256 | 4096 | 0.000760 | 0.000740 | 0.001774 | 0.001667 | 0.000279 | 0.006792 | 9648779.816546 | PASS |
| 3 | 16384 | 32 | 16 | 512 | 16384 | 0.003054 | 0.003190 | 0.007562 | 0.007183 | 0.001128 | 0.028800 | 9102091.691747 | PASS |
| 4 | 65536 | 64 | 16 | 1024 | 65536 | 0.014259 | 0.014521 | 0.032055 | 0.030789 | 0.004575 | 0.131659 | 7964346.004852 | PASS |
| 5 | 262144 | 128 | 16 | 2048 | 262144 | 0.055787 | 0.056406 | 0.155149 | 0.149298 | 0.018273 | 0.635224 | 6602870.272124 | PASS |
| 6 | 65536 | 64 | 32 | 1024 | 65536 | 0.029328 | 0.029976 | 0.062254 | 0.062173 | 0.009118 | 0.246781 | 8498039.683023 | PASS |
| 7 | 262144 | 128 | 32 | 2048 | 262144 | 0.122157 | 0.122187 | 0.303867 | 0.296419 | 0.036767 | 1.153705 | 7271016.908517 | PASS |

Phase 4F changes RM-VOLE_PPRF to use regular sparse blocks: one nonzero per block and one vector-PPRF tree over `blockSize = N/t` per block. Thus each sparse vector expands `t * blockSize = N` leaves instead of `t * N` leaves.

| `N` | `t` | Block size | Previous leaves / sparse vector | Phase 4F leaves / sparse vector | Leaves over `N` |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1024 | 8 | 128 | 8192 | 1024 | 1.000000 |
| 4096 | 16 | 256 | 65536 | 4096 | 1.000000 |
| 16384 | 32 | 512 | 524288 | 16384 | 1.000000 |
| 65536 | 64 | 1024 | 4194304 | 65536 | 1.000000 |
| 262144 | 128 | 2048 | 33554432 | 262144 | 1.000000 |

After regular-block PPRF setup, local expansion/WHT is the largest named cost:

| `N` | `m` | PPRF setup share of total | P0+P1 expansion share | Verification share |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 8 | 21.87% | 49.77% | 3.99% |
| 4096 | 16 | 22.08% | 50.66% | 4.11% |
| 16384 | 16 | 21.68% | 51.20% | 3.92% |
| 65536 | 16 | 21.86% | 47.73% | 3.47% |
| 262144 | 16 | 17.66% | 47.93% | 2.88% |
| 65536 | 32 | 24.03% | 50.42% | 3.69% |
| 262144 | 32 | 21.18% | 52.03% | 3.19% |

The remaining unaccounted time is base sampling, regular sparse sampling, payload construction, row-share initialization, and other local glue.

## RM-VOLE Local Algebra Only

| Preset | `N` | `t` | `m` | Output elems `m*N` | Gen s | Expand P0 s | Expand P1 s | Verify s | Total with verify s | Throughput elems/s | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 1024 | 8 | 8 | 8192 | 0.000361 | 0.000204 | 0.000208 | 0.000038 | 0.000811 | 10102137.806366 | PASS |
| 2 | 4096 | 16 | 16 | 65536 | 0.002565 | 0.001582 | 0.001581 | 0.000283 | 0.006011 | 10903127.950136 | PASS |
| 3 | 16384 | 32 | 16 | 262144 | 0.010793 | 0.006934 | 0.006819 | 0.001147 | 0.025692 | 10203353.577184 | PASS |

The optimized local algebra/WHT layer is not the bottleneck at these sizes. Integrating the prototype PPRF setup increases the total time by roughly:

| `N` | `m` | Algebra-only total s | RM-VOLE_PPRF total s | Multiplier |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 8 | 0.000811 | 0.000891 | 1.10x |
| 4096 | 16 | 0.006011 | 0.006740 | 1.12x |
| 16384 | 16 | 0.025692 | 0.028852 | 1.12x |

## Isolated Shared-Path Vector PPRF

| Preset | `N` | `t` | `m` | Output elems `m*N` | Setup s | Expansion s | Verify s | Total s | Throughput elems/s | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 1024 | 8 | 8 | 8192 | 0.000048 | 0.000862 | 0.000025 | 0.000934 | 8767580.849286 | PASS |
| 2 | 4096 | 16 | 16 | 65536 | 0.000329 | 0.014347 | 0.000212 | 0.014888 | 4401901.476322 | PASS |
| 3 | 16384 | 32 | 16 | 262144 | 0.001223 | 0.116167 | 0.000858 | 0.118248 | 2216897.616084 | PASS |

RM-VOLE_PPRF invokes this vector-PPRF structure twice: once for `Delta*s` and once for `Delta*e`. After Phase 4E, the integrated path streams those outputs directly into RM-VOLE row shares instead of first building standalone matrices.

## QA-SD VOLE Baseline

The command `./build/main --QA_VOLE k` passes `N = 2^k` into the original QA-SD VOLE path.

| `k` | `N` | QA_VOLE Fp64 s | QA_VOLE Fp32 s | Result |
| ---: | ---: | ---: | ---: | --- |
| 10 | 1024 | 0.0146174 | 0.0136537 | PASS |
| 12 | 4096 | 0.0146926 | 0.0140186 | PASS |
| 14 | 16384 | 0.0163816 | 0.0150491 | PASS |
| 16 | 65536 | 0.0237585 | 0.0195801 | PASS |
| 18 | 262144 | 0.0581570 | 0.0422254 | PASS |

## Estimated `m` Independent QA-SD VOLEs

This estimate multiplies the measured Fp64 QA-SD VOLE time by `m`. It is a cost comparison for producing `m*N` field correlations, not an equivalence of functionality.

| `N` | `t` | `m` | RM-VOLE_PPRF total s | QA_VOLE Fp64 s | Estimated `m * QA_VOLE` s | Estimated speedup vs `m * QA` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1024 | 8 | 8 | 0.000878 | 0.0146174 | 0.1169392 | 133.19x |
| 4096 | 16 | 16 | 0.006792 | 0.0146926 | 0.2350816 | 34.61x |
| 16384 | 32 | 16 | 0.028800 | 0.0163816 | 0.2621056 | 9.10x |
| 65536 | 64 | 16 | 0.131659 | 0.0237585 | 0.3801360 | 2.89x |
| 262144 | 128 | 16 | 0.635224 | 0.0581570 | 0.9305120 | 1.46x |
| 65536 | 64 | 32 | 0.246781 | 0.0237585 | 0.7602720 | 3.08x |
| 262144 | 128 | 32 | 1.153705 | 0.0581570 | 1.8610240 | 1.61x |

For the tested presets, the prototype RM-VOLE_PPRF path is faster than the rough `m * QA_VOLE` estimate. Phase 4F materially improves the comparison by using the regular sparse-noise structure.

The advantage remains positive at `N = 2^16` and `N = 2^18`, but the margin narrows as the WHT/output-expansion work grows.

## Raw Output

```text
MODULE_MVOLE_PPRF Fp64 N=1024 t=8 m=8 output_elems=8192 total_blocks=8 block_size=128 expanded_leaves_per_sparse_vector=1024 expected_N=1024 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.000098 pprf_e_setup_s=0.000094 expand_p0_s=0.000240 expand_p1_s=0.000197 verify_s=0.000035 total_s=0.000878 throughput_elems_per_s=9329121.720851 shared_path=1 PASS
MODULE_MVOLE_PPRF Fp64 N=4096 t=16 m=16 output_elems=65536 total_blocks=16 block_size=256 expanded_leaves_per_sparse_vector=4096 expected_N=4096 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.000760 pprf_e_setup_s=0.000740 expand_p0_s=0.001774 expand_p1_s=0.001667 verify_s=0.000279 total_s=0.006792 throughput_elems_per_s=9648779.816546 shared_path=1 PASS
MODULE_MVOLE_PPRF Fp64 N=16384 t=32 m=16 output_elems=262144 total_blocks=32 block_size=512 expanded_leaves_per_sparse_vector=16384 expected_N=16384 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.003054 pprf_e_setup_s=0.003190 expand_p0_s=0.007562 expand_p1_s=0.007183 verify_s=0.001128 total_s=0.028800 throughput_elems_per_s=9102091.691747 shared_path=1 PASS
MODULE_MVOLE_PPRF Fp64 N=65536 t=64 m=16 output_elems=1048576 total_blocks=64 block_size=1024 expanded_leaves_per_sparse_vector=65536 expected_N=65536 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.014259 pprf_e_setup_s=0.014521 expand_p0_s=0.032055 expand_p1_s=0.030789 verify_s=0.004575 total_s=0.131659 throughput_elems_per_s=7964346.004852 shared_path=1 PASS
MODULE_MVOLE_PPRF Fp64 N=262144 t=128 m=16 output_elems=4194304 total_blocks=128 block_size=2048 expanded_leaves_per_sparse_vector=262144 expected_N=262144 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.055787 pprf_e_setup_s=0.056406 expand_p0_s=0.155149 expand_p1_s=0.149298 verify_s=0.018273 total_s=0.635224 throughput_elems_per_s=6602870.272124 shared_path=1 PASS
MODULE_MVOLE_PPRF Fp64 N=65536 t=64 m=32 output_elems=2097152 total_blocks=64 block_size=1024 expanded_leaves_per_sparse_vector=65536 expected_N=65536 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.029328 pprf_e_setup_s=0.029976 expand_p0_s=0.062254 expand_p1_s=0.062173 verify_s=0.009118 total_s=0.246781 throughput_elems_per_s=8498039.683023 shared_path=1 PASS
MODULE_MVOLE_PPRF Fp64 N=262144 t=128 m=32 output_elems=8388608 total_blocks=128 block_size=2048 expanded_leaves_per_sparse_vector=262144 expected_N=262144 expanded_leaves_over_N=1.000000 pprf_s_setup_s=0.122157 pprf_e_setup_s=0.122187 expand_p0_s=0.303867 expand_p1_s=0.296419 verify_s=0.036767 total_s=1.153705 throughput_elems_per_s=7271016.908517 shared_path=1 PASS

MODULE_MVOLE_BENCH Fp64 N=1024 t=8 m=8 output_elems=8192 gen_base_s=0.000071 direct_masks_s=0.000290 gen_s=0.000361 expand_p0_s=0.000204 expand_p1_s=0.000208 x_wht_s=0.000008 z0_wht_m_s=0.000120 z1_wht_m_s=0.000121 verify_s=0.000038 total_no_verify_s=0.000773 total_with_verify_s=0.000811 throughput_no_verify_elems_per_s=10600563.311313 throughput_with_verify_elems_per_s=10102137.806366 micro_wht_1_s=0.000008 micro_wht_m_s=0.000111 PASS
MODULE_MVOLE_BENCH Fp64 N=4096 t=16 m=16 output_elems=65536 gen_base_s=0.000376 direct_masks_s=0.002188 gen_s=0.002565 expand_p0_s=0.001582 expand_p1_s=0.001581 x_wht_s=0.000032 z0_wht_m_s=0.000978 z1_wht_m_s=0.000994 verify_s=0.000283 total_no_verify_s=0.005727 total_with_verify_s=0.006011 throughput_no_verify_elems_per_s=11442414.566170 throughput_with_verify_elems_per_s=10903127.950136 micro_wht_1_s=0.000032 micro_wht_m_s=0.000830 PASS
MODULE_MVOLE_BENCH Fp64 N=16384 t=32 m=16 output_elems=262144 gen_base_s=0.002378 direct_masks_s=0.008415 gen_s=0.010793 expand_p0_s=0.006934 expand_p1_s=0.006819 x_wht_s=0.000139 z0_wht_m_s=0.004336 z1_wht_m_s=0.004334 verify_s=0.001147 total_no_verify_s=0.024545 total_with_verify_s=0.025692 throughput_no_verify_elems_per_s=10680101.672628 throughput_with_verify_elems_per_s=10203353.577184 micro_wht_1_s=0.000139 micro_wht_m_s=0.003563 PASS

MODULE_VECTOR_PPRF_TEST Fp64 N=1024 t=8 m=8 output_elems=8192 setup_s=0.000062 expand_s=0.000835 verify_s=0.000025 total_s=0.000921 throughput_elems_per_s=8890725.908781 shared_path=1 PASS
MODULE_VECTOR_PPRF_TEST Fp64 N=4096 t=16 m=16 output_elems=65536 setup_s=0.000329 expand_s=0.014347 verify_s=0.000212 total_s=0.014888 throughput_elems_per_s=4401901.476322 shared_path=1 PASS
MODULE_VECTOR_PPRF_TEST Fp64 N=16384 t=32 m=16 output_elems=262144 setup_s=0.001223 expand_s=0.116167 verify_s=0.000858 total_s=0.118248 throughput_elems_per_s=2216897.616084 shared_path=1 PASS

QA_VOLE 10: Fp64 0.0146174 s, Fp32 0.0136537 s
QA_VOLE 12: Fp64 0.0146926 s, Fp32 0.0140186 s
QA_VOLE 14: Fp64 0.0163816 s, Fp32 0.0150491 s
QA_VOLE 16: Fp64 0.0237585 s, Fp32 0.0195801 s
QA_VOLE 18: Fp64 0.0581570 s, Fp32 0.0422254 s
```

## Conclusion

After Phase 4G, RM-VOLE_PPRF is faster than the rough `m * QA_VOLE` Fp64 estimate for every implemented preset: about `133.19x` at `N=2^10,m=8`, `34.61x` at `N=2^12,m=16`, `9.10x` at `N=2^14,m=16`, `2.89x` at `N=2^16,m=16`, `1.46x` at `N=2^18,m=16`, `3.08x` at `N=2^16,m=32`, and `1.61x` at `N=2^18,m=32`.

The advantage should be stated conservatively. RM-VOLE_PPRF provides the shared-right-vector rank-1/tensor correlation, not `m` independent VOLE correlations. The comparison is favorable only for workloads that naturally need this structure.

The regular-block change removes the previous `O(tN)` PPRF blowup by reducing expanded leaves from `t*N` to `N` per sparse vector. The advantage remains positive at `N=2^16` and `N=2^18`, but the margin narrows as output/WHT work grows. This is still a local semi-honest prototype validating the RM-VOLE algebra and regular-block vector-PPRF setup, not a networked silent protocol. The dominant named cost is now P0/P1 row-wise WHT expansion. The next optimization target is batched or parallel row-wise WHT, or direct WHT-domain share generation.

## Final Summary

- Regular-block setup reduces expanded PPRF leaves from `t*N` to `N` per sparse vector.
- The measured `expanded_leaves_over_N` is `1.000000` for every RM-VOLE_PPRF preset.
- RM-VOLE_PPRF remains faster than the rough `m * QA_VOLE` estimate on the tested grid.
- The advantage remains at `N=2^16` and `N=2^18`, with speedups from `1.46x` to `3.08x` depending on `m`.
- The current dominant named cost is P0/P1 row-wise WHT expansion.
- Next optimization target: batched/parallel row-wise WHT or direct WHT-domain share generation.
- Caveat: this is a local semi-honest prototype, not a networked silent protocol.
