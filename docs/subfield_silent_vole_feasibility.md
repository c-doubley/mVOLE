# libOTe Silent Subfield VOLE Feasibility

Phase 6I audited whether libOTe's silent VOLE implementation can replace the Phase 6G generic noisy subfield VOLE baseline.

## Short Answer

Not for the same variable-`m` subfield shape used by the current noisy harness.

libOTe provides `SilentVoleSender<F,G,Ctx>` and `SilentVoleReceiver<F,G,Ctx>` with the desired relation:

```text
receiver outputs (a, c)
sender outputs (b, Delta)
a = b + c * Delta
```

This maps to the evaluated relation by the same sign convention:

```text
xhat = c
Z1 = a
Z0 = -b
Z0 + Z1 = Delta * xhat
```

However, the intended generic subfield instantiation

```cpp
F   = std::array<u8, m>
G   = u8
Ctx = CoeffCtxArray<u8, m>
```

does not currently compile through libOTe's silent VOLE encoder path in this checkout. The noisy VOLE API supports that shape, but silent VOLE has additional encoder code paths that assume compatible `F`/`G` iterator element types.

## API Findings

Files inspected:

- `libOTe/libOTe/Vole/Silent/SilentVoleSender.h`
- `libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h`
- `libOTe/frontend/ExampleVole.cpp`
- `libOTe/libOTe_Tests/Vole_Tests.cpp`
- `docs/rmvole_network_baseline_audit.md`

### 1. Relation

Yes, silent VOLE implements:

```text
a = b + c * Delta
```

with receiver values `(a,c)` and sender values `(b,Delta)`.

### 2. Field Types

The templates are generic:

```cpp
template<typename F, typename G = F, typename Ctx = DefaultCoeffCtx<F, G>>
class SilentVoleSender;

template<typename F, typename G = F, typename Ctx = DefaultCoeffCtx<F, G>>
class SilentVoleReceiver;
```

Known tested/used shapes in this checkout:

- `F = block`, `G = block`, `Ctx = CoeffCtxGF128`
- examples primarily run `Vole_example<block, block>`
- tests include active silent tests for `block, block` with some multiplication types

The test file contains commented examples for:

```cpp
SilentVole<std::array<u32, 8>, u32, CoeffCtxArray<u32, 8>>
```

but those tests are commented out and are not evidence that this shape builds in the current configuration.

### 3. Semi-Honest Mode

Yes. `SilentVoleSender` and `SilentVoleReceiver` default to:

```cpp
SilentSecType::SemiHonest
```

Malicious mode is only marked supported for:

```cpp
F = block
Ctx = CoeffCtxGF128
```

so the generic array subfield shape should be treated as semi-honest only.

### 4. Setup/Base-OT/Noisy-VOLE Prerequisites

Silent VOLE requires base material before the large silent expansion.

In `SilentBaseType::Base` mode, `genSilentBaseOts()` uses:

- a base OT protocol, selected by libOTe and falling back to `DefaultBaseOT`;
- a small libOTe `NoisyVole` instance to generate the sparse base VOLE correlations;
- `RegularPprf` base OTs for the sparse/noise vector.

In `SilentBaseType::BaseExtend` mode, the implementation can use SoftSpoken OT extension when enabled.

### 5. TCP/asio

Yes in principle. The silent API accepts coproto sockets, and `libOTe/frontend/ExampleVole.cpp` uses:

```cpp
auto chl = cp::asioConnect(ip, role == Role::Sender);
receiver.silentReceive(C, A, prng, chl);
sender.silentSend(delta, B, prng, chl);
```

So a TCP/asio harness is straightforward for supported `F,G,Ctx` instantiations.

### 6. Byte Counters

Yes. As with the noisy harness, bytes can be measured using coproto socket counters after flushing:

```cpp
socket.bytesSent()
socket.bytesReceived()
```

For two-process TCP, each process can report local sent/received bytes; matched sender/client rows can be aggregated by summing role-local sent bytes, or by using either side's `sent+received` local socket total.

### 7. Parameters `N` and `m`

For silent VOLE:

```cpp
configure(requestSize, ...)
```

sets `N = requestSize`, the number of generated VOLE correlations.

For the desired generic subfield harness, `m` would be the compile-time coordinate-array length in:

```cpp
std::array<u8, m>
```

This is the same interpretation as the noisy harness: an array length in byte coordinates, not RM-VOLE's odd-prime extension dimension.

For the supported `block, block` path, there is no variable `m`; the field is fixed GF(2^128)-style block arithmetic.

### 8. Does Silent Avoid `O(m^2 N)`?

Conceptually, yes. Silent VOLE should avoid running the full noisy `O(m^2 N)` payload for the large output. It still uses a small noisy VOLE during base setup, but the large expansion is based on PPRF plus an LPN-style code.

For the desired `array<u8,m>` shape, this remains conceptual because the current libOTe silent implementation does not compile through the relevant encoder path.

## Compile Experiment

An isolated harness was temporarily added for:

```cpp
SilentVoleSender<std::array<u8, M>, u8, CoeffCtxArray<u8, M>>
SilentVoleReceiver<std::array<u8, M>, u8, CoeffCtxArray<u8, M>>
```

using `SilentBaseType::Base`, `DefaultBaseOT`, and TCP/local coproto sockets.

Build failed before any benchmark could run. The representative error was from `EACode::dualEncode()` instantiated inside `SilentVoleReceiver::silentReceiveInplace()`:

```text
error: no matching function for call to
std::span<std::array<unsigned char, 8>>::span(
    __gnu_cxx::__normal_iterator<unsigned char*, std::span<unsigned char>>,
    ...)

error: cannot convert 'std::array<unsigned char, 64>' to 'unsigned char' in assignment
```

Reason: the silent receiver switch compiles encoder branches where `mC` has `G = u8` elements but the selected template path tries to encode/copy as if the iterator element type were `F = std::array<u8,m>`. This is not a runtime parameter issue; it is a template/API compatibility issue for `F != G` with the current encoder code.

The temporary harness code was removed after the failed build so no protocol or benchmark code remains modified for this phase.

## Feasibility Conclusion

For Phase 6I:

- **Usable as same-shape replacement for noisy `array<u8,m>` baseline:** No, not in the current timeline.
- **Usable for `block, block` silent VOLE:** Yes, but that is not a subfield/extension pair with variable `m`.
- **Potentially usable for `block, bool`:** The API is suggestive, but this path was not implemented here because it would not match the Phase 6G variable-`m` baseline and would need separate verification/normalization.
- **Needs libOTe internal changes for `array<u8,m>, u8`:** Likely yes. At minimum, the silent encoder branches need to avoid instantiating incompatible `F`/`G` paths or provide a compatible dual-encode path for mixed `F`/`G` array/scalar contexts.

## Should Silent Replace Noisy As Main External Baseline?

Not yet.

Silent VOLE is the right direction and should eventually replace the noisy baseline, but only after one of these is available:

1. a working same-shape silent subfield instantiation, ideally `array<u8,m>, u8` or a closer same-field representation;
2. a clearly documented `block,bool` binary-subfield baseline with bit-level normalization;
3. libOTe-side fixes enabling mixed `F != G` silent array contexts.

Until then, the Phase 6G noisy baseline should remain labeled as a **preliminary generic noisy subfield VOLE stress baseline**, not a SOTA comparison.

## Recommended Next Step

Phase 6J should either:

- implement a `block,bool` silent VOLE TCP harness and compare it with bit-normalized units only; or
- patch/audit libOTe's silent encoder path for `CoeffCtxArray<G,M>` so `F=array<G,M>, G=G` compiles without instantiating incompatible encoder branches.

Only after that should the paper use a silent libOTe baseline in the main comparison table.
