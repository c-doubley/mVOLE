# RM-VOLE PPRF Network Setup Test

Phase 6E-1 adds `RmvolePprfNetSetupTest.h` and CLI flag `--RMVOLE_PPRF_NET_SETUP`.

## API Used

- libOTe API: `RegularPprfSender<F,F,Ctx>` and `RegularPprfReceiver<F,F,Ctx>` from `libOTe/Tools/Pprf/RegularPprf.h`.
- Field instantiation: `F = u64`, `Ctx = CoeffCtxIntegerPrime_64`.
- Network channel: `coproto::LocalAsyncSocket::makePair()`.
- Base OT: `DefaultBaseOT::send()` and `DefaultBaseOT::receive()` are run over the same local async socket pair before each scalar RegularPprf expansion.
- PPRF expansion: sender calls `expand(socket, beta, seed, senderOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)` and receiver calls `expand(socket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)`.

## Relation Tested

For `N = 2^logN`, `blockSize = N/t`, and each block `i`, the harness centrally samples an offset `offset_i`, a nonzero scalar `s_i`, and extension-coordinate scalars `Delta_h`. For coordinate `h`, it programs:

```text
beta_{i,h} = Delta_h * s_i in F_p
```

RegularPprf reconstructs `receiverOut = senderOut + beta` at the selected point and `receiverOut = senderOut` elsewhere. The RM-VOLE setup shares are interpreted as:

```text
share0_h[j] = -senderOut_h[j]
share1_h[j] =  receiverOut_h[j]
share0_h[j] + share1_h[j] = beta_{i,h} at j = i*blockSize + offset_i, else 0
```

Verification opens the simulated local shares and checks all `m*N` scalar positions.

## Caveats

- This is a correctness/API harness, not secure input generation. The test centrally samples `Delta`, `s`, offsets, and then installs receiver choice bits.
- Phase 6E-1 runs `m` independent scalar RegularPprf instances for sparse `s` only. Sparse `e` is the next duplicate path, not included here.
- This is not the final optimized shared-path vector PPRF. It repeats base OT and PPRF setup independently per coordinate.
- It uses true coproto sockets and libOTe base OT/PPRF network messages in one process, but it is not a two-process TCP benchmark yet.

## Next Step

Phase 6E-2 should split the same sender/receiver work over TCP loopback/LAN. After that, implement the shared-path vector-valued PPRF so path/base material is paid once while coordinate payloads remain vector-valued.
