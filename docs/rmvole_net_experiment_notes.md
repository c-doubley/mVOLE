# RM-VOLE Network Setup Plus Expand Benchmark

Phase 6F adds `--RMVOLE_NET_BENCH`, a semi-honest harness that runs real vector-valued libOTe `RegularPprf` setup over coproto sockets and then performs local deterministic RM-VOLE expansion on each role.

## Relation

The benchmark checks the coordinate representation of `Z0 + Z1 = Delta * x`, where `x` is in `R_p`, and `Delta`, `Z0`, and `Z1` are represented as `m` base-field coordinate rows. Verification uses the existing `moduleMvoleVerify()` relation.

## Protocol Shape

- Setup uses two vector-valued `RegularPprf` executions: one for `Delta*s`, one for `Delta*e`.
- The regular PPRF domain is `domainSize = blockSize = N/t`, with `pointCount = t`.
- P0 interprets sender output as `v0 = -senderOut` and `u0 = -senderOut` for the two sparse vectors.
- P1 interprets receiver output as `v1 = receiverOut` and `u1 = receiverOut`.
- P0 expands locally as `x = WHT(rho*s + e)` and `Z0 = WHT(rho*v0) + WHT(u0)`.
- P1 expands locally as `Z1 = WHT(rho*v1) + WHT(u1)`.

## Harness Caveat

This is not secure input generation. The server samples the test instance and sends the client the common rho seed, Delta coordinates, and sparse offsets needed for choices/local expansion. These bytes are reported as `harness_metadata_bytes` and excluded from `clean_protocol_bytes` in bench mode. Verify mode may also send P0 openings after timing; those bytes are reported as `verification_opening_bytes`.

## Commands

```bash
./build/main --RMVOLE_NET_BENCH local 12 8 8 verify
./build/main --RMVOLE_NET_BENCH local 12 8 8 bench
./build/main --RMVOLE_NET_BENCH server 0.0.0.0 12300 12 8 8 3 bench
./build/main --RMVOLE_NET_BENCH client 127.0.0.1 12300 12 8 8 3 bench
```

Results are appended to `docs/rmvole_net_benchmark.csv`.

## Limitations

- Semi-honest correctness/API harness only; no malicious checks.
- Centralized test input generation remains a harness artifact.
- Supported vector dimensions are fixed compile-time `m = 8, 16, 32, 64`.
- This reports networked setup plus local expansion, not a fully integrated production protocol.
