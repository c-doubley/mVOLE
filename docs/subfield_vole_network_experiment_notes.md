# Subfield VOLE Network Benchmark Notes

This Phase 6B/6C harness benchmarks libOTe generic noisy subfield VOLE as an external networked baseline. It does not modify or measure RM-VOLE network runtime.

## Implementation

- Harness: `SubfieldVoleNetBench.h`
- CLI: `./build/main --SUBFIELD_VOLE_NET_BENCH <log2N>`
- libOTe classes: `NoisyVoleSender<F,G,Ctx>` and `NoisyVoleReceiver<F,G,Ctx>`
- Base OT path: libOTe `DefaultBaseOT`
- In-process socket path: `coproto::LocalAsyncSocket::makePair()`
- TCP socket path: `coproto::asioConnect(address, server)`
- Field instantiation: `F = std::array<u8, m>`, `G = u8`, `Ctx = CoeffCtxArray<u8, m>`

## Relation

libOTe noisy VOLE outputs receiver values `(a,c)` and sender values `(b,Delta)` such that:

```text
a = b + c * Delta
```

For the evaluated relation, the harness uses:

```text
xhat = c
Z1 = a
Z0 = -b
Z0 + Z1 = c * Delta
```

The harness verifies this relation after every trial.

## Network And Bytes

The default numeric benchmark is single-process `loopback-inproc`: both roles run in one process over real asynchronous `coproto` sockets. Communication is measured from socket counters, not estimated: `Socket::bytesSent()` on the sender and receiver endpoints after the protocol finishes. The measurement includes the libOTe `DefaultBaseOT` messages used by the noisy VOLE call and the noisy VOLE payload on the same socket abstraction.

This mode is useful for deterministic regression and relation checking, but it is not a two-process TCP measurement.

## Two-Process TCP Mode

Phase 6C adds two-process TCP mode using `coproto::asioConnect(address, server)`. The sender is the server/listener and the receiver is the client/connector. Example loopback commands:

```bash
./build/main --SUBFIELD_VOLE_NET_BENCH server 0.0.0.0 12120 14 16 3
./build/main --SUBFIELD_VOLE_NET_BENCH client 127.0.0.1 12120 14 16 3
```

For two machines, run the server with a bind address/port reachable from the client and replace `127.0.0.1` in the client command with the server machine's LAN IP. TCP rows are written to `docs/subfield_vole_tcp_loopback.csv`. Each process writes its own row because each process only knows its local socket counters. For a matching sender/client run, total wire bytes are the sum of `bytes_sent_by_role` over the two rows. The local `bytes_sent + bytes_received` value is also printed for debugging but should not be summed across both roles without noting the double count.

Phase 6C loopback commands tested on one host:

```bash
./build/main --SUBFIELD_VOLE_NET_BENCH server 0.0.0.0 12120 12 8 3
./build/main --SUBFIELD_VOLE_NET_BENCH client 127.0.0.1 12120 12 8 3
./build/main --SUBFIELD_VOLE_NET_BENCH server 0.0.0.0 12121 14 16 3
./build/main --SUBFIELD_VOLE_NET_BENCH client 127.0.0.1 12121 14 16 3
```

## Phase 6D TCP Loopback Grid

Phase 6D ran the larger two-process TCP loopback grid with `reps=3` and fresh ports `12200` through `12209`. The raw per-role rows are appended to `docs/subfield_vole_tcp_loopback.csv`. The aggregate rows are in `docs/subfield_vole_tcp_loopback_summary.csv`.

All requested grid points completed successfully:

| log2N | N | m | sender median s | client median s | total wire bytes | entries | entries/s | bytes/entry |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12 | 4096 | 8 | 0.021017802 | 0.019981095 | 6298176 | 32768 | 1559059.315527 | 192.205078 |
| 12 | 4096 | 16 | 0.057223168 | 0.056307988 | 25179072 | 65536 | 1145270.391181 | 384.202148 |
| 12 | 4096 | 32 | 0.138102718 | 0.137908322 | 100689600 | 131072 | 949090.661633 | 768.200684 |
| 12 | 4096 | 64 | 0.463360000 | 0.459555694 | 402705600 | 262144 | 565745.856354 | 1536.199951 |
| 14 | 16384 | 8 | 0.066535750 | 0.065745314 | 25172544 | 131072 | 1969948.486340 | 192.051270 |
| 14 | 16384 | 16 | 0.212170866 | 0.209018431 | 100676544 | 262144 | 1235532.497662 | 384.050537 |
| 14 | 16384 | 32 | 0.501728666 | 0.507499674 | 402679488 | 524288 | 1033080.466570 | 768.050171 |
| 16 | 65536 | 8 | 0.257735621 | 0.251839278 | 100670016 | 524288 | 2034208.534955 | 192.012817 |
| 16 | 65536 | 16 | 0.838305149 | 0.810208813 | 402666432 | 1048576 | 1250828.533322 | 384.012634 |
| 18 | 262144 | 8 | 1.025925311 | 0.995609145 | 402659904 | 2097152 | 2044156.604301 | 192.003204 |

The aggregate `entries/s` column uses `entries / max(sender median s, client median s)`. `total wire bytes` is the sum of the sender and client `bytes_sent_by_role` counters for the matched run. No Phase 6D grid point failed, hit OOM, or was rejected by the payload guard.

## Supported Parameters And Limitations

The noisy generic API supports compile-time coordinate dimensions in this harness for `m = 8, 16, 32, 64`. However, noisy VOLE communication grows with the binary decomposition size of `Delta`; for `std::array<u8,m>`, the dominant payload is about `8 * m^2 * N` bytes. The harness therefore marks settings above a 256 MiB estimated noisy payload as unsupported by the benchmark guard.

This is a generic subfield-VOLE baseline, not an exact same-field comparison to RM-VOLE over odd-prime `F_p`/`F_{p^m}`. The chosen `u8` coordinate-array context is useful for exercising the libOTe generic `F != G` subfield API and real socket communication, but it should be described in the paper as a networked generic subfield VOLE baseline only.

Silent subfield VOLE remains pending in this harness. The audited `SilentVole<F,G,Ctx>` API is the likely next target, but it needs careful parameter and setup treatment before reporting.

## Paper Use

Use these numbers as an external networked baseline showing the cost of a real libOTe subfield VOLE channel. Do not compare them as RM-VOLE LAN runtime. Current RM-VOLE PPRF measurements remain local prototype timings until vector-valued PPRF setup is implemented as a real sender/receiver protocol.
