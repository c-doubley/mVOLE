# RM-VOLE PPRF Network Setup Test

Phase 6E adds `RmvolePprfNetSetupTest.h` and CLI flag `--RMVOLE_PPRF_NET_SETUP`.

## Modes

```bash
./build/main --RMVOLE_PPRF_NET_SETUP local <logN> <t> <m> [verify|bench] [scalar|vector]
./build/main --RMVOLE_PPRF_NET_SETUP server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps> [verify|bench] [scalar|vector]
./build/main --RMVOLE_PPRF_NET_SETUP client <host> <port> <logN> <t> <m> <reps> [verify|bench] [scalar|vector]
```

The optional correctness mode defaults to `verify` for backward compatibility. `verify` opens shares for correctness. `bench` runs only `DefaultBaseOT` plus `RegularPprf` setup and does not exchange correctness-opening data. The optional implementation mode defaults to `scalar`, which is the Phase 6E-4 coordinate-wise path. `vector` selects the Phase 6E-6 fixed-size vector payload path.

Example TCP loopback run:

```bash
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12250 12 8 8 3 bench vector
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12250 12 8 8 3 bench vector
```

## API Used

- libOTe API: `RegularPprfSender<F,F,Ctx>` and `RegularPprfReceiver<F,F,Ctx>` from `libOTe/Tools/Pprf/RegularPprf.h`.
- Scalar field instantiation: `F = u64`, `Ctx = CoeffCtxIntegerPrime_64`.
- Vector field instantiation: `F = std::array<u64, M>`, `Ctx = CoeffCtxPrimeArray64<M>` for `M in {8,16,32,64}`. The context implements coordinate-wise arithmetic modulo `2^61 - 1`, not native integer arithmetic.
- Local channel: `coproto::LocalAsyncSocket::makePair()`.
- TCP channel: `coproto::asioConnect(address, server)`, with server running `RegularPprfSender` and client running `RegularPprfReceiver`.
- Base OT: `DefaultBaseOT::send()` and `DefaultBaseOT::receive()` are run before every RegularPprf instance.
- PPRF expansion: sender calls `expand(socket, beta, seed, senderOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)` and receiver calls `expand(socket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)`.

## Phase 6E-4 Batching Audit

`RegularPprf` is already a multi-point regular PPRF API. `configure(domainSize, pointCount)` sets the leaf domain for each tree and the number of punctured trees. `baseOtCount()` returns `log2(domainSize) * pointCount`, and `expand()` outputs `domainSize * pointCount` leaves in `ByTreeIndex` format.

The correct regular-block model for RM-VOLE setup is `domainSize = blockSize = N/t` and `pointCount = t`: one tree per regular block, one puncture per tree, and total output `t * blockSize = N` leaves per coordinate per sparse vector. A single `RegularPprf` configured as `domainSize = N, pointCount = t` would output `t*N` leaves and would not match the desired one-puncture-per-block layout without extra projection, so it is not the right model.

The scalar implementation mode is therefore `batched-regular-block`: one batched scalar `RegularPprf` for each coordinate of `s`, and one for each coordinate of `e`. This means `pprf_instances_s = m`, `pprf_instances_e = m`, and `total_pprf_instances = 2*m`. Since the harness still calls `DefaultBaseOT` once per batched `RegularPprf` instance, `default_base_ot_calls = 2*m` per role. The API clears base OT state after expansion, so this phase does not attempt to reuse the same base OT material across multiple `RegularPprf` instances.

## Phase 6E-6 Vector Mode

`implementation_mode=vector-fixed-prime` instantiates `RegularPprf` with fixed-size vector leaves, `F = std::array<u64, M>`, and a project-local `CoeffCtxPrimeArray64<M>`. The context supplies the operations required by `RegularPprf`: coordinate-wise `plus`, `minus`, `mul` by scalar where needed, `fromBlock`, `serialize`, `deserialize`, `copy`, `zero`, and equality. Every coordinate is reduced modulo the same `Fp64` prime used by the scalar RM-VOLE prototype.

Vector mode keeps the same regular-block shape, `domainSize = blockSize` and `pointCount = t`, but pays for one vector-valued `RegularPprf` for `s` and one for `e`. Therefore `pprf_instances_s = 1`, `pprf_instances_e = 1`, `total_pprf_instances = 2`, and `default_base_ot_calls = 2`. The expanded scalar-coordinate count remains `2*m*N`.

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

In `verify` mode, local mode opens simulated shares in one process and checks all `2*m*N` scalar positions. TCP verify mode has the client centrally generate correctness-test inputs and send each `beta` payload to the server; after each PPRF, the server sends its output share back to the client so the client can open and verify. This metadata/share opening is only for the correctness harness.

In `bench` mode, the sender locally samples the programmed payloads and the receiver locally samples its regular-block choices. The parties still run real `DefaultBaseOT` and `RegularPprf` over the selected socket, but they do not send `beta`, `senderOut`, or reconstruction-opening data.

## Counters And Bytes

Scalar mode has `pprf_instances_s = m`, `pprf_instances_e = m`, `total_pprf_instances = 2*m`, and `default_base_ot_calls = 2*m`. Vector mode has `pprf_instances_s = 1`, `pprf_instances_e = 1`, `total_pprf_instances = 2`, and `default_base_ot_calls = 2`. Both modes report `expanded_leaves = 2*m*N`. TCP rows are appended to `docs/rmvole_pprf_tcp_setup_clean.csv`.

`bench` socket counters are the clean protocol measurement for this harness: `DefaultBaseOT` plus `RegularPprf` only. `verify` socket counters include test metadata (`beta` sent from client to server) and verification opening traffic (`senderOut` sent from server to client). The `harness_metadata_bytes` column is a payload-size estimate for those correctness messages; message framing can make `verify - bench` slightly larger.

## Phase 6E-6 TCP Comparison Results

Commands tested on one host:

```bash
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12250 12 8 8 3 bench vector
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12250 12 8 8 3 bench vector
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12251 14 16 16 3 bench vector
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12251 14 16 16 3 bench vector
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12252 12 8 8 3 bench scalar
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12252 12 8 8 3 bench scalar
./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12253 14 16 16 3 bench scalar
./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12253 14 16 16 3 bench scalar
```

| impl | logN | t | m | role | total setup s | clean protocol bytes | total PPRFs | DefaultBaseOT calls |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| vector | 12 | 8 | 8 | sender-server | 0.011227 | 39600 | 2 | 2 |
| vector | 12 | 8 | 8 | receiver-client | 0.011168 | 39600 | 2 | 2 |
| scalar | 12 | 8 | 8 | sender-server | 0.077653 | 230448 | 16 | 16 |
| scalar | 12 | 8 | 8 | receiver-client | 0.077652 | 230448 | 16 | 16 |
| vector | 14 | 16 | 16 | sender-server | 0.024220 | 109824 | 2 | 2 |
| vector | 14 | 16 | 16 | receiver-client | 0.024182 | 109824 | 2 | 2 |
| scalar | 14 | 16 | 16 | sender-server | 0.324359 | 1019184 | 32 | 32 |
| scalar | 14 | 16 | 16 | receiver-client | 0.324514 | 1019184 | 32 | 32 |

The vector mode keeps the vector correction payload proportional to `m`, but it removes repeated scalar tree/base-OT setup. At `logN=12,t=8,m=8`, clean TCP bytes dropped from 230,448 to 39,600. At `logN=14,t=16,m=16`, clean TCP bytes dropped from 1,019,184 to 109,824.

## Caveats

- This is a semi-honest correctness/API harness, not secure input generation.
- It uses real coproto sockets and libOTe `DefaultBaseOT`/`RegularPprf` network messages.
- It still uses centralized test input generation.
- Vector mode supports fixed compile-time `m = 8, 16, 32, 64`.
- It is setup only, not the full RM-VOLE expand path or LAN runtime for the whole construction.

## Next Step

Connect the vector-valued setup harness to the full RM-VOLE setup state and define the honest network measurement boundary for setup plus local expand. The remaining security caveat is replacing centralized test input generation with real role-owned inputs.
