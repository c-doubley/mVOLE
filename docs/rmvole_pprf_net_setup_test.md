# RM-VOLE PPRF Network Setup Test

Phase 6E adds `RmvolePprfNetSetupTest.h` and CLI flag `--RMVOLE_PPRF_NET_SETUP`.

## Modes

```bash
./build/main --RMVOLE_PPRF_NET_SETUP local <logN> <t> <m> [verify|bench] [batched|batched-regular]
./build/main --RMVOLE_PPRF_NET_SETUP server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps> [verify|bench] [batched|batched-regular]
./build/main --RMVOLE_PPRF_NET_SETUP client <host> <port> <logN> <t> <m> <reps> [verify|bench] [batched|batched-regular]
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

## Phase 6E-4 Batching Audit

`RegularPprf` is already a multi-point regular PPRF API. `configure(domainSize, pointCount)` sets the leaf domain for each tree and the number of punctured trees. `baseOtCount()` returns `log2(domainSize) * pointCount`, and `expand()` outputs `domainSize * pointCount` leaves in `ByTreeIndex` format.

The correct regular-block model for RM-VOLE setup is `domainSize = blockSize = N/t` and `pointCount = t`: one tree per regular block, one puncture per tree, and total output `t * blockSize = N` leaves per coordinate per sparse vector. A single `RegularPprf` configured as `domainSize = N, pointCount = t` would output `t*N` leaves and would not match the desired one-puncture-per-block layout without extra projection, so it is not the right model.

The current implementation mode is therefore `batched-regular-block`: one batched scalar `RegularPprf` for each coordinate of `s`, and one for each coordinate of `e`. This means `scalar_pprf_instances_s = m`, `scalar_pprf_instances_e = m`, and `total_scalar_pprf_instances = 2*m`. Since the harness still calls `DefaultBaseOT` once per batched `RegularPprf` instance, `default_base_ot_calls = 2*m` per role. The API clears base OT state after expansion, so this phase does not attempt to reuse the same base OT material across multiple `RegularPprf` instances.

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

`scalar_pprf_instances_s = m`, `scalar_pprf_instances_e = m`, `total_scalar_pprf_instances = 2*m`, `default_base_ot_calls = 2*m`, `expanded_leaves_per_coordinate_per_sparse_vector = N`, and `total_scalar_expanded_leaves = 2*m*N`. TCP rows are appended to `docs/rmvole_pprf_tcp_setup_clean.csv`.

`bench` socket counters are the clean protocol measurement for this harness: `DefaultBaseOT` plus `RegularPprf` only. `verify` socket counters include test metadata (`beta` sent from client to server) and verification opening traffic (`senderOut` sent from server to client). The `harness_metadata_bytes` column is a payload-size estimate for those correctness messages; message framing can make `verify - bench` slightly larger. The repeated `DefaultBaseOT` per scalar PPRF is a likely overestimate and a future batching/reuse target.

## Phase 6E-4 TCP Smoke Results

Commands tested on one host:

```bash
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12242 12 8 8 3 verify
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12242 12 8 8 3 verify
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12240 12 8 8 3 bench
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12240 12 8 8 3 bench
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12241 14 16 16 3 bench
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12241 14 16 16 3 bench
```

| mode | logN | t | m | role | median total setup s | local socket bytes | clean protocol bytes | harness metadata bytes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| verify | 12 | 8 | 8 | sender-server | 0.079683 | 1807152 | 231216 | 1575936 |
| verify | 12 | 8 | 8 | receiver-client | 0.079687 | 1807152 | 231216 | 1575936 |
| bench | 12 | 8 | 8 | sender-server | 0.077437 | 230448 | 230448 | 0 |
| bench | 12 | 8 | 8 | receiver-client | 0.077430 | 230448 | 230448 | 0 |
| bench | 14 | 16 | 16 | sender-server | 0.329987 | 1019184 | 1019184 | 0 |
| bench | 14 | 16 | 16 | receiver-client | 0.330009 | 1019184 | 1019184 | 0 |

At `logN=12,t=8,m=8`, verify mode used 1,807,152 local socket bytes per role while bench mode used 230,448. The verification-only difference was 1,576,704 bytes per role, dominated by the `beta` and `senderOut` correctness traffic.

## Caveats

- This is a semi-honest correctness/API harness, not secure input generation.
- It uses real coproto sockets and libOTe `DefaultBaseOT`/`RegularPprf` network messages.
- It still uses centralized test input generation.
- It runs `m` independent batched scalar RegularPprf instances for `s` and another `m` for `e`; it is not the final shared-path vector-valued PPRF.
- It is setup only, not the full RM-VOLE expand path or LAN runtime for the whole construction.

## Next Step

Batch or reuse base OT material across scalar PPRFs where valid, then replace the coordinate-wise adapter with a shared-path vector-valued PPRF so path/base material is paid once and only coordinate payloads scale with `m`.
