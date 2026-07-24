# Silent sVOLE Security Notes

All SILENT_SVOLE rows use libOTe's `SilentVoleSender/Receiver` in semi-honest mode with `secParam=128` and `MultType=ExConv7x24`. The RM parameter `t=64` is not reused. The selected regular-LPN noise weight, partition count, code size, base noisy-VOLE length, and base OT count are recorded per point.

Rows are skipped only by the documented resource guards. No ad hoc parameters are chosen by the driver.
