# Local RM-VOLE Benchmark v2

Benchmark source commit: `b33a10fa7851ba0c3a26bea531ffb004f00d9365`.

The v2 bundle measures fixed parameters p=2^61-1, lambda=128, c=2, t=64, logN in {12,14,16,18,20}, and m in {8,16,32}. RM_VECTOR and RM_COORD use the same split-input noisy-sVOLE length 2t, sparse supports, coefficients, fixed-basis representation, WHT, pointwise operations, and output relation. The intended backend difference is two vector-valued RegularPprf instances for RM_VECTOR versus 2m scalar RegularPprf instances for RM_COORD.

No LAN, WAN, or netem experiment is run by this script. This report is an engineering artifact index and does not make paper-level performance claims.
