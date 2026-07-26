# Local Expansion Analysis

RM expansion rows are generated from the existing timed RM expansion harness and the optimized RM TCP runs. The WHT-based RM expansion is component-measured separately from split setup and PPRF setup. The ExConv rows are taken from silent sVOLE component timing and parameter rows. These encoders are not claimed to implement the same code; the comparison is a component-cost comparison of local expansion mechanisms.

At the local sizes tested previously, WHT expansion is very fast at small `N`; the ratio narrows as `N` and `m` grow because row-wise module-coordinate WHT and output materialization become the dominant local subphases.
