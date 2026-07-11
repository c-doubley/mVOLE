# RM-VOLE Network Setup Plus Expand Benchmark

Phase 6F adds `--RMVOLE_NET_BENCH`, a semi-honest harness that runs real vector-valued libOTe `RegularPprf` setup over coproto sockets and then performs local deterministic RM-VOLE expansion on each role.

Phase 6K adds an optional `split-input` setup mode. This mode flips the PPRF roles so that P0 is the `RegularPprfReceiver` with sparse support choices and P1 is the `RegularPprfSender` programming masked payload shares. The current Phase 6K split-input product layer is deliberately marked `split-input-simulated`: a harness helper samples additive product shares for `Delta*s_i` and `Delta*e_i` and sends P1's programmed `-B_i` shares. This verifies the wiring and accounting shape, but it is not yet a secure OT/VOLE product-sharing protocol.

## Relation

The benchmark checks the coordinate representation of `Z0 + Z1 = Delta * x`, where `x` is in `R_p`, and `Delta`, `Z0`, and `Z1` are represented as `m` base-field coordinate rows. Verification uses the existing `moduleMvoleVerify()` relation.

## Protocol Shape

- Setup uses two vector-valued `RegularPprf` executions: one for `Delta*s`, one for `Delta*e`.
- The regular PPRF domain is `domainSize = blockSize = N/t`, with `pointCount = t`.
- In `centralized` mode, P0 is the PPRF sender and interprets sender output as `v0 = -senderOut` and `u0 = -senderOut`; P1 is the PPRF receiver and uses `receiverOut`.
- In `split-input-simulated` mode, P0 is the PPRF receiver, adds its simulated product share `A_i` at selected leaves, and P1 is the PPRF sender storing `-senderOut`.
- P0 expands locally as `x = WHT(rho*s + e)` and `Z0 = WHT(rho*v0) + WHT(u0)`.
- P1 expands locally as `Z1 = WHT(rho*v1) + WHT(u1)`.

In `centralized` mode, the harness computes the full `Delta*s_i` and `Delta*e_i` payloads and gives them directly to `RegularPprfSender`. In `split-input` mode, the harness simulates shares `A_i - B_i = Delta*q_i`; P1 programs `-B_i`, P0 receives the punctured output and adds `A_i` at the selected leaf, and the reconstructed sparse payload is still `Delta*q_i`.

## Harness Caveat

`centralized` mode is not secure input generation. The server samples the test instance and sends the client the common rho seed, Delta coordinates, and sparse offsets needed for choices/local expansion. These bytes are reported as `harness_metadata_bytes` and excluded from `clean_protocol_bytes` in bench mode. Verify mode may also send P0 openings after timing; those bytes are reported as `verification_opening_bytes`.

`split-input-simulated` mode removes full-product PPRF programming and does not send sparse offsets to P1, but it still centrally creates product shares. The `split_input_bytes` column counts the simulated payload bytes for transferring P1's programmed product shares; in TCP mode it is payload-level accounting and does not try to split coproto framing bytes at the send boundary. It is not the cost of a secure split-input primitive. A complete semi-honest implementation still needs scalar/subfield VOLE or OT-based multiplication sharing for the `2t` sparse coefficients.

## Commands

```bash
./build/main --RMVOLE_NET_BENCH local 12 8 8 verify
./build/main --RMVOLE_NET_BENCH local 12 8 8 bench
./build/main --RMVOLE_NET_BENCH local 12 8 8 verify split-input
./build/main --RMVOLE_NET_BENCH local 12 8 8 bench split-input
./build/main --RMVOLE_NET_BENCH server 0.0.0.0 12300 12 8 8 3 bench centralized
./build/main --RMVOLE_NET_BENCH client 127.0.0.1 12300 12 8 8 3 bench centralized
./build/main --RMVOLE_NET_BENCH server 0.0.0.0 12350 12 8 8 3 bench split-input
./build/main --RMVOLE_NET_BENCH client 127.0.0.1 12350 12 8 8 3 bench split-input
```

Results are appended to `docs/rmvole_net_benchmark.csv`. The detailed split/PPRF timing and byte breakdown is also appended to `docs/rmvole_split_input_benchmark.csv`.

## Limitations

- Semi-honest correctness/API harness only; no malicious checks.
- `split-input` currently means `split-input-simulated`; secure scalar/subfield VOLE or OT multiplication sharing remains to be implemented.
- Supported vector dimensions are fixed compile-time `m = 8, 16, 32, 64`.
- This reports networked setup plus local expansion, not a fully integrated production protocol.
