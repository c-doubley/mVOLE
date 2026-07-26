# Current Split Backend Audit

The current generic split backend instantiates libOTe noisy subfield VOLE with `F = ExtElem`, `G = BaseElem`, `L = 2t = 128`, and `ExtElem = array<u64,M>`.

The dominant sender payload follows `L * ctx.bitSize<ExtElem>() * ctx.byteSize<ExtElem>()`. Since `ctx.bitSize<ExtElem>() = 64m` and `ctx.byteSize<ExtElem>() = 8m`, the dominant term is `128 * 512 * m^2` bytes, so it scales as `O(t*m^2)`.

The artifact distinguishes the abstract centralized `PCG.Gen`, distributed seed setup, PPRF programming, and split-input product sharing. The measured current backend is only the split-input product-sharing primitive plus transport framing, not the centralized sampler.
