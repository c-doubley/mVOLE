# RM-VOLE PPRF Network Setup Test

Phase 6E adds `RmvolePprfNetSetupTest.h` and CLI flag `--RMVOLE_PPRF_NET_SETUP`.

## Modes

```bash
./build/main --RMVOLE_PPRF_NET_SETUP local <logN> <t> <m>
./build/main --RMVOLE_PPRF_NET_SETUP server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps>
./build/main --RMVOLE_PPRF_NET_SETUP client <host> <port> <logN> <t> <m> <reps>
```

Example TCP loopback run:

```bash
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12220 12 8 8 3
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12220 12 8 8 3
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

Local mode opens simulated shares in one process and checks all `2*m*N` scalar positions. TCP mode has the client centrally generate correctness-test inputs and send each scalar `beta` vector to the server; after each PPRF, the server sends its output share back to the client so the client can open and verify. This metadata/share opening is only for the correctness harness.

## Counters And Bytes

`scalar_pprf_count_s = m`, `scalar_pprf_count_e = m`, `total_scalar_pprf_count = 2*m`, `expanded_leaves_per_coordinate_per_sparse_vector = N`, and `total_scalar_expanded_leaves = 2*m*N`. TCP rows are appended to `docs/rmvole_pprf_tcp_setup.csv`.

The socket byte counters in TCP mode are local per process and include test metadata (`beta` sent from client to server) and verification opening traffic (`senderOut` sent from server to client), in addition to `DefaultBaseOT` and `RegularPprf` messages. The repeated `DefaultBaseOT` per scalar PPRF is a likely overestimate and a future batching/reuse target.

## Phase 6E-2 TCP Smoke Results

Commands tested on one host:

```bash
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12220 12 8 8 3
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12220 12 8 8 3

./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12221 14 16 16 3
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12221 14 16 16 3
```

Both `s` and `e` passed reconstruction in the client verifier.

| logN | t | m | role | median s setup s | median e setup s | median total setup s | verify s | bytes sent | bytes received |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12 | 8 | 8 | sender-server | 0.040039 | 0.039952 | 0.080028 | 0.000000 | 1686168 | 120984 |
| 12 | 8 | 8 | receiver-client | 0.040050 | 0.039947 | 0.080043 | 0.000222 | 120984 | 1686168 |
| 14 | 16 | 16 | sender-server | 0.165284 | 0.166144 | 0.332708 | 0.000000 | 13080600 | 535320 |
| 14 | 16 | 16 | receiver-client | 0.165296 | 0.166145 | 0.332746 | 0.001741 | 535320 | 13080600 |

## Caveats

- This is a semi-honest correctness/API harness, not secure input generation.
- It uses real coproto sockets and libOTe `DefaultBaseOT`/`RegularPprf` network messages.
- It still uses centralized test input generation.
- It runs `m` independent scalar RegularPprf instances for `s` and another `m` for `e`; it is not the final shared-path vector-valued PPRF.
- It is setup only, not the full RM-VOLE expand path or LAN runtime for the whole construction.

## Next Step

Batch or reuse base OT material across scalar PPRFs where valid, then replace the coordinate-wise adapter with a shared-path vector-valued PPRF so path/base material is paid once and only coordinate payloads scale with `m`.
