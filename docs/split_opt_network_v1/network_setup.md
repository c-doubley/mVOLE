# Controlled Network Setup

The generator first tries to create two Linux network namespaces, `rmv-p0` and `rmv-p1`, joined by a veth pair. If the host forbids namespace creation, it falls back to applying `tc netem` to loopback and records `topology=loopback_tc`. Loopback WAN profiles use a calibrated configured `tc` rate to hit the named effective target. Each profile runs an iperf3 JSON calibration. Protocol benchmarks for a profile are run only if measured throughput is within 15% of the named target rate; otherwise the profile is recorded as aborted and no protocol timing rows are accepted for that profile.

The measured network matrix is a representative end-to-end RM point: `logN=16`, `m=16`, `t=64`, with vector/coordinate generic split and coefficient-bit OT optimized split methods. Full local scaling is kept in `rm_opt_local_raw.csv`.
