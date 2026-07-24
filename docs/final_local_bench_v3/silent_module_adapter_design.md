# Silent Module Adapter Design

The exact baseline uses `ExtElem = std::array<u64,M>`, `BaseElem = u64`, and `CoeffCtxPrimeArray64<M>` over `p=2^61-1`.

No project-local protocol adapter was needed. A minimal libOTe header repair was required because two receiver ExAcc branches instantiated `EACode::dualEncode` for the base vector with `F` instead of `G`. The patch changes only those two template arguments. It does not alter the code family, noise sampling, LPN parameters, messages, or security checks.

Patch:

```diff
diff --git a/libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h b/libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h
index 469261c..6baaa17 100644
--- a/libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h
+++ b/libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h
@@ -539,7 +539,7 @@ namespace osuCrypto
 				 EACode encoder;
 				encoder.config(mRequestSize, mNoiseVecSize, weight);
 				 encoder.dualEncode<F,Ctx>(mA.begin(), mCtx);
-				 encoder.dualEncode<F,Ctx>(mC.begin(), mCtx);
+				 encoder.dualEncode<G,Ctx>(mC.begin(), mCtx);
 				 break;
 			}
 			case osuCrypto::MultType::ExAcc40:
@@ -548,7 +548,7 @@ namespace osuCrypto
 				 EACode encoder;
 				encoder.config(mRequestSize, mNoiseVecSize, weight);
 				 encoder.dualEncode<F,Ctx>(mA.begin(), mCtx);
-				 encoder.dualEncode<F,Ctx>(mC.begin(), mCtx);
+				 encoder.dualEncode<G,Ctx>(mC.begin(), mCtx);
 				 break;
 			}
 			case osuCrypto::MultType::QuasiCyclic:
```
