# Silent R2C Split Design

The `SPLIT_SILENT_R2C` backend runs exact silent subfield VOLE over `F_{p^m}/F_p` to obtain `a = b + c*Delta`, then P0 sends `d=q-c` and P1 computes `b_prime=b-d*Delta`. The outputs are `A=a`, `B=b_prime`, and verification checks `A-B=q*Delta` coordinate-wise.

Security note: `c` is uniform and hidden from P1, so `d=q-c` is uniform from P1's view. Sending `d` does not reveal `q`, and the conversion does not change P0's chosen nonzero distribution of `q`.
