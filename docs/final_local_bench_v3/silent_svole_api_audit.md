# Silent sVOLE API Audit

Audited implementation paths:

- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/Vole/Silent/SilentVoleSender.h`: class template lines 33-38; base VOLE relation lines 86-96; `configure` lines 223-239; `silentBaseOtCount` lines 246-252; `silentSend` lines 289-300; ExConv encode lines 377-392.
- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h`: class template lines 37-42; output buffers `mA`, `mC` lines 97-103; `configure` lines 257-271; sampled base values lines 301-367; `silentReceive` lines 393-407; relation comment lines 459-476; ExConv mixed encode lines 516-533.
- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/Vole/Noisy/NoisyVoleSender.h`: chosen-Delta sender relation lines 50-69.
- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/Vole/Noisy/NoisyVoleReceiver.h`: chosen-c receiver relation lines 44-62 and `2^i*c[j]` computation lines 100-120.
- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/Tools/ExConvCode/ExConvCode.h`: mixed `dualEncode2` lines 116-128; encode implementation lines 267-312.
- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/Tools/EACode/EACode.h`: same-type iterator overload lines 42-49; mixed `dualEncode2` lines 51-57; previous failure site lines 96-124.
- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/TwoChooseOne/ConfigureCode.h`: `SilentSecType` include path through `TcoOtDefines.h`, `MultType`, `DefaultMultType=ExConv7x24`, and parameter selector lines 116-163.
- `/home/cyy/pcg-experiments/mVOLE-local-silent-bench/libOTe/libOTe/TwoChooseOne/Silent/SilentOtExtUtil.h`: `SilentSecType` enum.

Answers:

1. The API produces `a = b + c * Delta`; v3 normalizes this as `x=c`, `Z0=a`, `Z1=-b`, so `Z0 + Z1 = x * Delta`.
2. The sender holds `Delta` and passes it to `SilentVoleSender::silentSend`.
3. The receiver receives/owns the base-field vector `c`.
4. Receiver outputs are `c` and `a`; sender output is `b`.
5. Yes. `SilentVoleSender<F,G,Ctx>` and `SilentVoleReceiver<F,G,Ctx>` distinguish module/output element `F` from coefficient/base-field type `G`.
6. The default ExConv encoder requires zero/copy/resize/serialization, coordinate-wise addition/subtraction, `mulConst`, random sampling, binary decomposition, and base-scalar multiplication for the noisy base VOLE. It does not require arbitrary `F*F`.
7. The previous EACode failure was caused by receiver ExAcc branches compiling `encoder.dualEncode<F,Ctx>(mC.begin(), mCtx)` even though `mC` is a `VecG`.
8. This was an incorrect template argument/iterator constraint interaction, not an algebraic incompatibility. The minimal repair is `dualEncode<G,Ctx>` for `mC`.
9. The silent code supports semi-honest mode; `mMalType` defaults to `SilentSecType::SemiHonest`, and v3 configures `secParam=128`.
10. v3 records the selected LPN/code parameters per output length in `silent_svole_parameters.csv`.

Probe results and failed compiler output are under `probe_logs/`. Source commit: `562e21bc6bf16bfc32ac3e2d6f21719f28965a23`.
