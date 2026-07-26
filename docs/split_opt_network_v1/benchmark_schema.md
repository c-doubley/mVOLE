# Benchmark Schema

All timing values ending in `_ms` are milliseconds. Communication values ending in `_bytes` are byte counts after subtracting verification openings where applicable. `trial_index=-2` is a correctness run, `trial_index=-1` is warmup, and nonnegative trials are measured samples. `network_profile=LOCAL_TCP` means two local TCP processes without traffic shaping; `LAN10G`, `WAN100`, and `WAN10` are controlled network profiles calibrated with iperf3 before protocol runs.
