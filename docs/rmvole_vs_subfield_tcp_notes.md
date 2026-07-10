# Preliminary RM-VOLE vs libOTe Noisy Subfield VOLE TCP Loopback Comparison

This note summarizes Phase 6G TCP loopback measurements for the evaluated relation shape `Z0 + Z1 = Delta * xhat`, with `entries = m*N`. This is a preliminary comparison with libOTe's generic **noisy** subfield VOLE API. It is not an apples-to-apples SOTA comparison.

Each TCP point used two processes on one host and `reps=3`; aggregate runtime is `max(server, client)` median. The original communication columns are cumulative over the three repetitions unless a `_per_rep` column is used.

## Methods

- `RM-VOLE-vector-PPRF-setup-local-expand`: semi-honest vector-valued `RegularPprf` network setup for `Delta*s` and `Delta*e`, followed by local deterministic WHT expansion. Clean communication excludes centralized harness metadata and also excludes the secure split-input generation of `Delta*s` and `Delta*e`.
- `libOTe-noisy-subfield-VOLE`: generic libOTe noisy subfield VOLE baseline over TCP loopback, instantiated as `F = std::array<u8,m>`, `G = u8`.

## Important Caveats

- RM-VOLE is a networked setup plus local expansion benchmark, not a fully production two-party protocol.
- RM-VOLE still uses centralized test-input harness metadata; those bytes are reported separately by the harness and excluded from clean protocol bytes.
- RM-VOLE does not include the real protocol cost for securely generating/programming `Delta*s` and `Delta*e` when `P0` owns `s,e` and `P1` owns `Delta`.
- The libOTe noisy instantiation used here has dominant communication `O(m^2 N)`: it binary-decomposes an `m`-byte `Delta` and sends `F`-sized corrections for each decomposition bit and each output.
- The original byte and bytes/entry columns are cumulative over `reps=3`; use the `_per_rep` columns in `docs/rmvole_vs_subfield_tcp.csv` for one-repetition communication.
- The comparison is semi-honest only and does not claim malicious security.
- The field representations differ: RM-VOLE uses odd-prime `Fp64` vector coordinates, while libOTe noisy subfield VOLE uses its generic subfield representation (`array<u8,m>` in this harness).

## Completed Grid

| logN | N | m | t | RM runtime s | libOTe runtime s | runtime ratio libOTe/RM | RM bytes/entry | libOTe bytes/entry | comm ratio libOTe/RM |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 12 | 4096 | 8 | 8 | 0.011620 | 0.020597 | 1.77x | 1.211426 | 192.205078 | 158.66x |
| 12 | 4096 | 16 | 8 | 0.014893 | 0.059088 | 3.97x | 0.793213 | 384.202148 | 484.36x |
| 12 | 4096 | 32 | 8 | 0.018692 | 0.139941 | 7.49x | 0.584106 | 768.200684 | 1315.17x |
| 12 | 4096 | 64 | 8 | 0.028257 | 0.466198 | 16.50x | 0.479553 | 1536.199951 | 3203.40x |
| 14 | 16384 | 8 | 16 | 0.027112 | 0.067137 | 2.48x | 0.651123 | 192.051270 | 294.95x |
| 14 | 16384 | 16 | 16 | 0.032482 | 0.214177 | 6.59x | 0.419312 | 384.050537 | 915.91x |
| 14 | 16384 | 32 | 16 | 0.044388 | 0.514477 | 11.59x | 0.303406 | 768.050171 | 2531.43x |
| 16 | 65536 | 8 | 64 | 0.114224 | 0.256220 | 2.24x | 0.648926 | 192.012817 | 295.89x |
| 16 | 65536 | 16 | 64 | 0.135087 | 0.840693 | 6.22x | 0.418213 | 384.012634 | 918.22x |
| 18 | 262144 | 8 | 128 | 0.389325 | 1.040719 | 2.67x | 0.348450 | 192.003204 | 551.02x |

## Figures

- `docs/rmvole_vs_subfield_figures/runtime_vs_N_tcp.{pdf,png}`
- `docs/rmvole_vs_subfield_figures/comm_per_entry_tcp.{pdf,png}`
- `docs/rmvole_vs_subfield_figures/throughput_tcp.{pdf,png}`

## Commands

RM-VOLE TCP example:

```bash
./build/main --RMVOLE_NET_BENCH server 0.0.0.0 <port> <logN> <t> <m> 3 bench
./build/main --RMVOLE_NET_BENCH client 127.0.0.1 <port> <logN> <t> <m> 3 bench
```

libOTe noisy subfield VOLE TCP example:

```bash
./build/main --SUBFIELD_VOLE_NET_BENCH server 0.0.0.0 <port> <logN> <m> 3
./build/main --SUBFIELD_VOLE_NET_BENCH client 127.0.0.1 <port> <logN> <m> 3
```

## Failed Points

None.
