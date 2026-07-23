# Known Limitations

- RM_VECTOR uses the checked split-input noisy-sVOLE plus vector RegularPprf harness.
- RM_COORD setup-inclusive end-to-end and subphase Expand breakdown are not integrated in this checkout; rows are fail-closed as skipped.
- DIRECT_NOISY_SVOLE fixed-prime preflight/model rows are produced, but measured direct rows are skipped unless the fixed-prime direct harness is added.
- Base OTs are included through libOTe `DefaultBaseOT`; a preprocessed-base-OT mode was not available uniformly across all methods.

The committed artifact bundle was generated with one measured repetition plus one warm-up per measured point, not the requested 10 measured repetitions. Use `reproduction_commands.sh` to run the full 10-repetition campaign.

Skipped row count: 60
