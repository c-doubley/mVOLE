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

Measured TCP loopback results:

| log2N | m | role | median_total_s | bytes_sent_by_role | bytes_received_by_role | total_local_socket_bytes |
| --- | --- | --- | --- | --- | --- | --- |
| 12 | 8 | receiver-client | 0.020833590 | 6291624 | 6552 | 6298176 |
| 12 | 8 | sender-server | 0.020986639 | 6552 | 6291624 | 6298176 |
| 14 | 16 | receiver-client | 0.265669563 | 100663464 | 13080 | 100676544 |
| 14 | 16 | sender-server | 0.268240809 | 13080 | 100663464 | 100676544 |

For the matched TCP loopback runs, total wire bytes by summing the two per-role send counters were 6,298,176 bytes for `log2N=12, m=8` and 100,676,544 bytes for `log2N=14, m=16`.

## Supported Parameters And Limitations

The noisy generic API supports compile-time coordinate dimensions in this harness for `m = 8, 16, 32, 64`. However, noisy VOLE communication grows with the binary decomposition size of `Delta`; for `std::array<u8,m>`, the dominant payload is about `8 * m^2 * N` bytes. The harness therefore marks settings above a 256 MiB estimated noisy payload as unsupported by the benchmark guard.

This is a generic subfield-VOLE baseline, not an exact same-field comparison to RM-VOLE over odd-prime `F_p`/`F_{p^m}`. The chosen `u8` coordinate-array context is useful for exercising the libOTe generic `F != G` subfield API and real socket communication, but it should be described in the paper as a networked generic subfield VOLE baseline only.

Silent subfield VOLE remains pending in this harness. The audited `SilentVole<F,G,Ctx>` API is the likely next target, but it needs careful parameter and setup treatment before reporting.

## Paper Use

Use these numbers as an external networked baseline showing the cost of a real libOTe subfield VOLE channel. Do not compare them as RM-VOLE LAN runtime. Current RM-VOLE PPRF measurements remain local prototype timings until vector-valued PPRF setup is implemented as a real sender/receiver protocol.
