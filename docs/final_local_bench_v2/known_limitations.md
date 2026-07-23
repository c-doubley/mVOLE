# Known Limitations

- Direct noisy-sVOLE rows use the local async socket pair command surface; RM_VECTOR and RM_COORD end-to-end rows use TCP loopback.
- If `numactl` is unavailable, process CPU placement uses `taskset` and the environment records that memory binding was not applied by numactl.
- PPRF PREPROCESSED_OT rows use an accounting convention because this checkout does not expose a separate CLI that times only after externally supplied base OTs.
- Expand subphase rows report aggregate per-party expand timings; WHT, pointwise operations, and materialization are included in that aggregate.
