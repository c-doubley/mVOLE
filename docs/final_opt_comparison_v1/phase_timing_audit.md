# Phase Timing Audit

Raw rows in this directory preserve separate `p0_split_input_setup_ms` and `p1_split_input_setup_ms` timers. The phase wall time is `max(P0,P1)` only for phases where both roles execute the same concurrent protocol interval; those rows are marked `split_input_phase_wall_definition=max(P0,P1)_same_phase`.

The optimized RM integrated split phase is compared against the standalone `SPLIT_COEFF_OT` benchmark. The coefficient-OT split input has fixed length `L=2t=128`, so material dependence on `N` is treated as a regression.

## Checks
- m=8, logN=12: integrated split median 30.733 ms vs standalone 30.613 ms, ratio 1.004, PASS.
- m=8, logN=14: integrated split median 30.721 ms vs standalone 30.613 ms, ratio 1.004, PASS.
- m=8, logN=16: integrated split median 30.849 ms vs standalone 30.613 ms, ratio 1.008, PASS.
- m=8, logN=18: integrated split median 30.803 ms vs standalone 30.613 ms, ratio 1.006, PASS.
- m=8, logN=20: integrated split median 30.818 ms vs standalone 30.613 ms, ratio 1.007, PASS.
- m=8: split-input median spread across N is 0.41%, PASS.
- m=16, logN=12: integrated split median 33.425 ms vs standalone 33.906 ms, ratio 0.986, PASS.
- m=16, logN=14: integrated split median 33.377 ms vs standalone 33.906 ms, ratio 0.984, PASS.
- m=16, logN=16: integrated split median 33.444 ms vs standalone 33.906 ms, ratio 0.986, PASS.
- m=16, logN=18: integrated split median 33.425 ms vs standalone 33.906 ms, ratio 0.986, PASS.
- m=16, logN=20: integrated split median 33.412 ms vs standalone 33.906 ms, ratio 0.985, PASS.
- m=16: split-input median spread across N is 0.20%, PASS.
- m=32, logN=12: integrated split median 38.260 ms vs standalone 37.525 ms, ratio 1.020, PASS.
- m=32, logN=14: integrated split median 38.121 ms vs standalone 37.525 ms, ratio 1.016, PASS.
- m=32, logN=16: integrated split median 38.251 ms vs standalone 37.525 ms, ratio 1.019, PASS.
- m=32, logN=18: integrated split median 37.915 ms vs standalone 37.525 ms, ratio 1.010, PASS.
- m=32, logN=20: integrated split median 38.115 ms vs standalone 37.525 ms, ratio 1.016, PASS.
- m=32: split-input median spread across N is 0.91%, PASS.
