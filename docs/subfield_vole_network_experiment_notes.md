# Subfield VOLE Network Benchmark Notes

This Phase 6B harness benchmarks libOTe generic noisy subfield VOLE as an external networked baseline. It does not modify or measure RM-VOLE network runtime.

## Implementation

- Harness: `SubfieldVoleNetBench.h`
- CLI: `./build/main --SUBFIELD_VOLE_NET_BENCH <log2N>`
- libOTe classes: `NoisyVoleSender<F,G,Ctx>` and `NoisyVoleReceiver<F,G,Ctx>`
- Base OT path: libOTe `DefaultBaseOT`
- Socket path: `coproto::LocalAsyncSocket::makePair()`
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

The benchmark is single-process loopback-LAN: both roles run in one process over real asynchronous `coproto` sockets. Communication is measured from socket counters, not estimated: `Socket::bytesSent()` on the sender and receiver endpoints after the protocol finishes. The measurement includes the libOTe `DefaultBaseOT` messages used by the noisy VOLE call and the noisy VOLE payload on the same socket abstraction.

The code is structured with separate sender and receiver role calls around one socket pair, so a later Phase 6C split can replace `LocalAsyncSocket::makePair()` with `asioConnect()` listen/connect roles.

## Supported Parameters And Limitations

The noisy generic API supports compile-time coordinate dimensions in this harness for `m = 8, 16, 32, 64`. However, noisy VOLE communication grows with the binary decomposition size of `Delta`; for `std::array<u8,m>`, the dominant payload is about `8 * m^2 * N` bytes. The harness therefore marks settings above a 256 MiB estimated noisy payload as unsupported by the benchmark guard.

This is a generic subfield-VOLE baseline, not an exact same-field comparison to RM-VOLE over odd-prime `F_p`/`F_{p^m}`. The chosen `u8` coordinate-array context is useful for exercising the libOTe generic `F != G` subfield API and real socket communication, but it should be described in the paper as a networked generic subfield VOLE baseline only.

Silent subfield VOLE remains pending in this harness. The audited `SilentVole<F,G,Ctx>` API is the likely next target, but it needs careful parameter and setup treatment before reporting.

## Phase 6B Run Grid

Successful noisy-VOLE median-of-3 points:

| log2N | N | m |
| ---: | ---: | ---: |
| 12 | 4096 | 8 |
| 12 | 4096 | 16 |
| 12 | 4096 | 32 |
| 12 | 4096 | 64 |
| 14 | 16384 | 8 |
| 14 | 16384 | 16 |
| 14 | 16384 | 32 |
| 16 | 65536 | 8 |
| 16 | 65536 | 16 |
| 18 | 262144 | 8 |

Unsupported by the noisy-payload guard:

| log2N | N | m | estimated noisy payload bytes |
| ---: | ---: | ---: | ---: |
| 14 | 16384 | 64 | 536870912 |
| 16 | 65536 | 32 | 536870912 |
| 16 | 65536 | 64 | 2147483648 |
| 18 | 262144 | 16 | 536870912 |
| 18 | 262144 | 32 | 2147483648 |
| 18 | 262144 | 64 | 8589934592 |

## Paper Use

Use these numbers as an external networked baseline showing the cost of a real libOTe subfield VOLE channel. Do not compare them as RM-VOLE LAN runtime. Current RM-VOLE PPRF measurements remain local prototype timings until vector-valued PPRF setup is implemented as a real sender/receiver protocol.
