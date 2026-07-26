# Coefficient-Bit OT Split Design

`SPLIT_COEFF_OT` decomposes each `q[i]` into 61 bits and batches `L*61 = 7808` chosen-message OTs in one Kos semi-honest OT-extension invocation per trial. Each OT transfers a seed; `ExtElem` messages are one-time padded with a domain-separated PRG stream and sent as ciphertext pairs. P0 sums selected messages into `A[i]`; P1 sums random masks into `B[i]`; verification checks `A[i]-B[i]=q[i]*Delta`.

This implementation does not invoke a base OT per bit; one base-OT setup feeds the whole OT extension batch.
