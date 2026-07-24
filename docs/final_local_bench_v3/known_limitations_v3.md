# Known Limitations v3

- Local-only TCP loopback benchmark; no LAN, WAN, netem, or cross-machine measurement.
- RM_VECTOR and RM_COORD measurements are preserved from frozen v2 artifacts and are not rerun.
- SILENT_SVOLE uses libOTe semi-honest 128-bit parameter selection with `ExConv7x24`; malicious mode is not claimed.
- Direct noisy sVOLE large unmeasured points remain modeled only and are kept separate from measured TCP points.
