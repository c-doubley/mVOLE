# RM-VOLE PPRF Network Setup Test

Phase 6E adds `RmvolePprfNetSetupTest.h` and CLI flag `--RMVOLE_PPRF_NET_SETUP`.

## Modes

```bash
./build/main --RMVOLE_PPRF_NET_SETUP local <logN> <t> <m> [verify|bench]
./build/main --RMVOLE_PPRF_NET_SETUP server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps> [verify|bench]
./build/main --RMVOLE_PPRF_NET_SETUP client <host> <port> <logN> <t> <m> <reps> [verify|bench]
```

The optional mode defaults to `verify` for backward compatibility. `verify` opens shares for correctness. `bench` runs only `DefaultBaseOT` plus `RegularPprf` setup and does not exchange correctness-opening data.

Example TCP loopback run:

```bash
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12220 12 8 8 3 verify
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12220 12 8 8 3 verify
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12221 12 8 8 3 bench
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12221 12 8 8 3 bench
```

## API Used

- libOTe API: `RegularPprfSender<F,F,Ctx>` and `RegularPprfReceiver<F,F,Ctx>` from `libOTe/Tools/Pprf/RegularPprf.h`.
- Field instantiation: `F = u64`, `Ctx = CoeffCtxIntegerPrime_64`.
- Local channel: `coproto::LocalAsyncSocket::makePair()`.
- TCP channel: `coproto::asioConnect(address, server)`, with server running `RegularPprfSender` and client running `RegularPprfReceiver`.
- Base OT: `DefaultBaseOT::send()` and `DefaultBaseOT::receive()` are run before every scalar RegularPprf instance.
- PPRF expansion: sender calls `expand(socket, beta, seed, senderOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)` and receiver calls `expand(socket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)`.

## Relation Tested

For `N = 2^logN`, `blockSize = N/t`, and each block `i`, the harness centrally samples sparse offsets and nonzero scalars for both `s` and `e`, plus extension-coordinate scalars `Delta_h`. It programs two sparse vectors:

```text
betaS_{i,h} = Delta_h * s_i in F_p
betaE_{i,h} = Delta_h * e_i in F_p
```

RegularPprf reconstructs `receiverOut = senderOut + beta` at the selected point and `receiverOut = senderOut` elsewhere. The RM-VOLE setup shares are interpreted as:

```text
share0_h[j] = -senderOut_h[j]
share1_h[j] =  receiverOut_h[j]
share0_h[j] + share1_h[j] = betaS or betaE at its support, else 0
```

In `verify` mode, local mode opens simulated shares in one process and checks all `2*m*N` scalar positions. TCP verify mode has the client centrally generate correctness-test inputs and send each scalar `beta` vector to the server; after each PPRF, the server sends its output share back to the client so the client can open and verify. This metadata/share opening is only for the correctness harness.

In `bench` mode, the sender locally samples the programmed scalar payloads and the receiver locally samples its regular-block choices. The parties still run real `DefaultBaseOT` and `RegularPprf` over the selected socket, but they do not send `beta`, `senderOut`, or reconstruction-opening data.

## Counters And Bytes

`scalar_pprf_count_s = m*t`, `scalar_pprf_count_e = m*t`, `total_scalar_pprf_count = 2*m*t`, `expanded_leaves_per_coordinate_per_sparse_vector = N`, and `total_scalar_expanded_leaves = 2*m*N`. TCP rows are appended to `docs/rmvole_pprf_tcp_setup_clean.csv`.

`bench` socket counters are the clean protocol measurement for this harness: `DefaultBaseOT` plus `RegularPprf` only. `verify` socket counters include test metadata (`beta` sent from client to server) and verification opening traffic (`senderOut` sent from server to client). The `harness_metadata_bytes` column is a payload-size estimate for those correctness messages; message framing can make `verify - bench` slightly larger. The repeated `DefaultBaseOT` per scalar PPRF is a likely overestimate and a future batching/reuse target.

## Phase 6E-3 TCP Smoke Results

Commands tested on one host:

```bash
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12230 12 8 8 3 verify
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12230 12 8 8 3 verify
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12231 12 8 8 3 bench
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12231 12 8 8 3 bench
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12232 14 16 16 3 bench
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12232 14 16 16 3 bench
```

| mode | logN | t | m | role | median total setup s | local socket bytes | clean protocol bytes | harness metadata bytes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| verify | 12 | 8 | 8 | sender-server | 0.124626 | 1807152 | 231216 | 1575936 |
| verify | 12 | 8 | 8 | receiver-client | 0.124652 | 1807152 | 231216 | 1575936 |
| bench | 12 | 8 | 8 | sender-server | 0.094229 | 230448 | 230448 | 0 |
| bench | 12 | 8 | 8 | receiver-client | 0.094204 | 230448 | 230448 | 0 |
| bench | 14 | 16 | 16 | sender-server | 0.401627 | 1019184 | 1019184 | 0 |
| bench | 14 | 16 | 16 | receiver-client | 0.401663 | 1019184 | 1019184 | 0 |

At `logN=12,t=8,m=8`, verify mode used 1,807,152 local socket bytes per role while bench mode used 230,448. The verification-only difference was 1,576,704 bytes per role, dominated by the `beta` and `senderOut` correctness traffic.

## Caveats

- This is a semi-honest correctness/API harness, not secure input generation.
- It uses real coproto sockets and libOTe `DefaultBaseOT`/`RegularPprf` network messages.
- It still uses centralized test input generation.
- It runs `m` independent scalar RegularPprf instances for `s` and another `m` for `e`; it is not the final shared-path vector-valued PPRF.
- It is setup only, not the full RM-VOLE expand path or LAN runtime for the whole construction.

## Next Step

Batch or reuse base OT material across scalar PPRFs where valid, then replace the coordinate-wise adapter with a shared-path vector-valued PPRF so path/base material is paid once and only coordinate payloads scale with `m`.
