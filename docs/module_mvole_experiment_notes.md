# ModuleMVOLE Semantic Baseline Experiment Notes

Baseline commit: `27ec6074f03e0a6d59bf2ad52becc988ebf98497` (`Implement semi-honest RM-VOLE prototype`).
Experiment branch: `module-mvole-semantic-baseline`.

## Environment

```text
hostname: cyy
nproc: 64
CPU: Intel(R) Xeon(R) Platinum 8269CY CPU T 3.10GHz, 2 sockets, 16 cores/socket, 2 threads/core
compiler: g++ (Ubuntu 11.4.0-2ubuntu1~20.04) 11.4.0
```

## Commands

```bash
cmake --build build --parallel 64
./build/main --MODULE_MVOLE_PPRF 1      # repeated 3 times for presets 1..7
./build/main --MODULE_MVOLE_COORD_PPRF 1 # repeated 3 times for presets 1..7
./build/main --QA_VOLE 10               # repeated 3 times for 10,12,14,16,18
```

Smoke tests also passed:

```bash
./build/main --MODULE_MVOLE_PPRF 1
./build/main --MODULE_MVOLE_COORD_PPRF 1
./build/main --MODULE_MVOLE 1
```

## Baselines

1. `--MODULE_MVOLE_PPRF`: shared-path vector-valued PPRF RM-VOLE. One regular-block tree is expanded per block, and all `m` coordinates are derived from the shared leaves. Expanded leaves per sparse vector: `N`.
2. `--MODULE_MVOLE_COORD_PPRF`: semantically equivalent coordinate-wise scalar PPRF RM-VOLE. It keeps the same regular-block support and same RM-VOLE relation, but runs a separate scalar PPRF for every coordinate. Scalar-coordinate expanded leaves per sparse vector: `m*N`.
3. `m * QA_VOLE`: generic cost proxy only. It is not semantically equivalent, because independent QA-SD VOLE runs produce independent right vectors while RM-VOLE produces one shared right vector `x`.

## Median Results

| N | t | m | vector RM-VOLE (s) | coord RM-VOLE (s) | coord/vector | m*QA proxy (s) | QA proxy/vector |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1024 | 8 | 8 | 0.000872 | 0.000989 | 1.13x | 0.144226 | 165.40x |
| 4096 | 16 | 16 | 0.006894 | 0.007266 | 1.05x | 0.294669 | 42.74x |
| 16384 | 32 | 16 | 0.028522 | 0.044866 | 1.57x | 0.267189 | 9.37x |
| 65536 | 64 | 16 | 0.126704 | 0.194964 | 1.54x | 0.372547 | 2.94x |
| 262144 | 128 | 16 | 0.632057 | 0.662134 | 1.05x | 0.909234 | 1.44x |
| 65536 | 64 | 32 | 0.242992 | 0.254451 | 1.05x | 0.745094 | 3.07x |
| 262144 | 128 | 32 | 1.182721 | 1.176052 | 0.99x | 1.818467 | 1.54x |

The vector path is faster than the coordinate-wise semantic baseline for most settings, from near parity up to about 1.6x in this run. The improvement from sharing PPRF paths grows when setup cost is visible, but it is muted at large N because P0/P1 row-wise WHT expansion dominates the total runtime.

Against the `m * QA_VOLE` proxy, vector RM-VOLE remains faster across the measured grid. This proxy is useful as a rough cost reference, not as a semantic baseline.

## Generated Artifacts

- `docs/module_mvole_semantic_baseline.csv`
- `docs/module_mvole_paper_table.csv`
- `docs/module_mvole_paper_figures/runtime_vs_N.pdf`
- `docs/module_mvole_paper_figures/runtime_vs_N.png`
- `docs/module_mvole_paper_figures/speedup_vs_coordinate_baseline.pdf`
- `docs/module_mvole_paper_figures/speedup_vs_coordinate_baseline.png`
- `docs/module_mvole_paper_figures/breakdown_percent.pdf`
- `docs/module_mvole_paper_figures/breakdown_percent.png`
- `docs/module_mvole_performance_evaluation.tex`

## Caveats

This is a local semi-honest prototype validating the RM-VOLE algebra and regular-block vector-PPRF setup direction. It is not a networked silent setup implementation and does not include base OT, serialization, communication, or malicious-security checks. No WHT parallel optimization is added in this baseline.