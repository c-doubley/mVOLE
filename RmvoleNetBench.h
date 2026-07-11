#pragma once

#include "ModuleMVOLE.h"
#include "ModuleMvolePprfIntegration.h"
#include "RmvolePprfNetSetupTest.h"
#include "coproto/Socket/AsioSocket.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "libOTe/Vole/Noisy/NoisyVoleReceiver.h"
#include "libOTe/Vole/Noisy/NoisyVoleSender.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace osuCrypto
{
    enum class RmvoleNetBenchMode
    {
        Verify,
        Bench
    };

    enum class RmvoleNetBenchSetupMode
    {
        Centralized,
        SplitInputSimulated,
        SplitInputVole
    };

    inline std::string rmvoleNetBenchModeName(RmvoleNetBenchMode mode)
    {
        return mode == RmvoleNetBenchMode::Bench ? "bench" : "verify";
    }

    inline RmvoleNetBenchMode rmvoleNetBenchParseMode(const std::string& value)
    {
        if (value == "verify")
            return RmvoleNetBenchMode::Verify;
        if (value == "bench")
            return RmvoleNetBenchMode::Bench;
        throw std::runtime_error("RMVOLE_NET_BENCH mode must be verify or bench.");
    }

    inline std::string rmvoleNetBenchSetupModeName(RmvoleNetBenchSetupMode mode)
    {
        if (mode == RmvoleNetBenchSetupMode::SplitInputVole)
            return "split-input-vole";
        return mode == RmvoleNetBenchSetupMode::SplitInputSimulated ? "split-input-simulated" : "centralized";
    }

    inline RmvoleNetBenchSetupMode rmvoleNetBenchParseSetupMode(const std::string& value)
    {
        if (value == "centralized")
            return RmvoleNetBenchSetupMode::Centralized;
        if (value == "split-input" || value == "split-input-simulated" || value == "simulated")
            return RmvoleNetBenchSetupMode::SplitInputSimulated;
        if (value == "split-input-vole" || value == "vole")
            return RmvoleNetBenchSetupMode::SplitInputVole;
        throw std::runtime_error("RMVOLE_NET_BENCH setup mode must be centralized, split-input, or split-input-vole.");
    }

    struct RmvoleNetBenchParams
    {
        u64 logN = 0;
        u64 N = 0;
        u64 t = 0;
        u64 m = 0;
        u64 blockSize = 0;
    };

    struct RmvoleNetBenchRow
    {
        std::string method = "RMVOLE-vector-RegularPprf-setup-local-expand";
        std::string mode = "bench";
        std::string setupMode = "centralized";
        std::string role = "local";
        std::string network = "local-async-pair";
        std::string address;
        u64 logN = 0;
        u64 N = 0;
        u64 t = 0;
        u64 m = 0;
        u64 blockSize = 0;
        u64 reps = 1;
        double splitInputSeconds = 0;
        double pprfSetupSeconds = 0;
        double setupSeconds = 0;
        double expandP0Seconds = 0;
        double expandP1Seconds = 0;
        double totalSeconds = 0;
        double verifySeconds = 0;
        u64 bytesSentByRole = 0;
        u64 bytesReceivedByRole = 0;
        u64 totalSocketBytes = 0;
        u64 harnessMetadataBytes = 0;
        u64 verificationOpeningBytes = 0;
        u64 splitInputBytes = 0;
        u64 pprfBytes = 0;
        u64 cleanProtocolBytes = 0;
        u64 entries = 0;
        double entriesPerSecond = 0;
        double bytesPerEntry = 0;
        u64 pprfInstances = 2;
        u64 defaultBaseOtCalls = 2;
        bool ok = false;
        std::string notes;
    };

    template<u64 M>
    struct RmvoleNetBenchSplitShares
    {
        typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>> p0Additive;
        typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>> p1ProgrammedNeg;
    };

    template<u64 M>
    struct RmvoleNetBenchInstance
    {
        block rhoSeed = ZeroBlock;
        RmvolePprfPrimeVector<M> delta{};
        RmvolePprfSparseInput<u64> sparseS;
        RmvolePprfSparseInput<u64> sparseE;
    };

    inline RmvoleNetBenchParams rmvoleNetBenchMakeParams(u64 logN, u64 t, u64 m)
    {
        auto p = rmvolePprfMakeParams(logN, t, m);
        return {p.logN, p.N, p.t, p.m, p.blockSize};
    }

    inline ModuleMVOLEParams rmvoleNetBenchModuleParams(const RmvoleNetBenchParams& params)
    {
        ModuleMVOLEParams out;
        out.n = params.logN;
        out.N = params.N;
        out.t = params.t;
        out.m = params.m;
        return out;
    }

    inline double rmvoleNetBenchMedian(std::vector<double> values)
    {
        return rmvolePprfMedian(std::move(values));
    }

    inline std::string rmvoleNetBenchCsvEscape(const std::string& value)
    {
        return rmvolePprfCsvEscape(value);
    }

    inline std::string rmvoleNetBenchTcpNetworkLabel(const std::string& host)
    {
        return rmvolePprfTcpNetworkLabel(host);
    }

    inline u64 rmvoleNetBenchSplitShareBytes(const RmvoleNetBenchParams& params)
    {
        return params.t * params.m * sizeof(u64);
    }

    inline u64 rmvoleNetBenchSplitMetadataBytes(const RmvoleNetBenchParams& params)
    {
        return sizeof(block) + params.m * sizeof(u64);
    }

    inline bool rmvoleNetBenchUsesSplitInput(RmvoleNetBenchSetupMode setupMode)
    {
        return setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated ||
            setupMode == RmvoleNetBenchSetupMode::SplitInputVole;
    }

    template<typename F, typename Ctx>
    void rmvoleNetBenchSampleRho(
        typename Ctx::template Vec<F>& rho,
        const RmvoleNetBenchParams& params,
        block seed,
        Ctx& ctx)
    {
        PRNG prng(seed);
        ctx.resize(rho, params.N);
        for (u64 j = 0; j < params.N; ++j)
            ctx.fromBlock(rho[j], prng.get<block>());
    }

    template<typename F, typename Ctx>
    void rmvoleNetBenchSparseToVec(
        typename Ctx::template Vec<F>& out,
        const RmvoleNetBenchParams& params,
        const RmvolePprfSparseInput<F>& sparse,
        Ctx& ctx)
    {
        ctx.resize(out, params.N);
        ctx.zero(out.begin(), out.end());
        for (u64 i = 0; i < params.t; ++i)
            out[i * params.blockSize + sparse.offsets[i]] = sparse.values[i];
    }

    template<u64 M>
    RmvoleNetBenchInstance<M> rmvoleNetBenchSampleInstance(
        const RmvoleNetBenchParams& params,
        PRNG& prng)
    {
        if (params.m != M)
            throw std::runtime_error("RMVOLE_NET_BENCH vector dispatch used the wrong compile-time m.");

        CoeffCtxIntegerPrime_64 scalarCtx;
        RmvoleNetBenchInstance<M> instance;
        instance.rhoSeed = prng.get<block>();
        instance.delta = rmvolePprfSampleDeltaVector<M>(
            RmvolePprfNetSetupParams{params.logN, params.N, params.t, params.m, params.blockSize},
            prng,
            scalarCtx);
        instance.sparseS = rmvolePprfSampleSparseInput<u64, CoeffCtxIntegerPrime_64>(
            RmvolePprfNetSetupParams{params.logN, params.N, params.t, params.m, params.blockSize},
            prng,
            scalarCtx);
        instance.sparseE = rmvolePprfSampleSparseInput<u64, CoeffCtxIntegerPrime_64>(
            RmvolePprfNetSetupParams{params.logN, params.N, params.t, params.m, params.blockSize},
            prng,
            scalarCtx);
        return instance;
    }

    template<u64 M>
    void rmvoleNetBenchBuildP0Gen(
        ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64>& gen,
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        CoeffCtxIntegerPrime_64& ctx)
    {
        typename CoeffCtxIntegerPrime_64::template Vec<u64> rhoS;

        rmvoleNetBenchSampleRho<u64, CoeffCtxIntegerPrime_64>(gen.base.rho, params, instance.rhoSeed, ctx);
        ctx.resize(gen.base.rhoWht, params.N);
        ctx.copy(gen.base.rho.begin(), gen.base.rho.end(), gen.base.rhoWht.begin());
        wht<u64, CoeffCtxIntegerPrime_64>(gen.base.rhoWht, static_cast<int>(params.N));

        rmvoleNetBenchSparseToVec<u64, CoeffCtxIntegerPrime_64>(gen.base.s, params, instance.sparseS, ctx);
        rmvoleNetBenchSparseToVec<u64, CoeffCtxIntegerPrime_64>(gen.base.e, params, instance.sparseE, ctx);
        moduleMvoleRingMulSparseRhs<u64, CoeffCtxIntegerPrime_64>(gen.base.rho, gen.base.s, rhoS, ctx);
        ctx.resize(gen.base.b, params.N);
        for (u64 j = 0; j < params.N; ++j)
            ctx.plus(gen.base.b[j], rhoS[j], gen.base.e[j]);

        ctx.resize(gen.Delta, params.m);
        for (u64 h = 0; h < params.m; ++h)
            gen.Delta[h] = instance.delta[h];
        moduleMvoleInitPprfRows<u64, CoeffCtxIntegerPrime_64>(
            rmvoleNetBenchModuleParams(params),
            gen.rowMasks,
            ctx);
    }

    template<u64 M>
    void rmvoleNetBenchBuildP1Gen(
        ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64>& gen,
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        CoeffCtxIntegerPrime_64& ctx)
    {
        rmvoleNetBenchSampleRho<u64, CoeffCtxIntegerPrime_64>(gen.base.rho, params, instance.rhoSeed, ctx);
        ctx.resize(gen.base.rhoWht, params.N);
        ctx.copy(gen.base.rho.begin(), gen.base.rho.end(), gen.base.rhoWht.begin());
        wht<u64, CoeffCtxIntegerPrime_64>(gen.base.rhoWht, static_cast<int>(params.N));

        ctx.resize(gen.Delta, params.m);
        for (u64 h = 0; h < params.m; ++h)
            gen.Delta[h] = instance.delta[h];
        moduleMvoleInitPprfRows<u64, CoeffCtxIntegerPrime_64>(
            rmvoleNetBenchModuleParams(params),
            gen.rowMasks,
            ctx);
    }

    template<u64 M>
    void rmvoleNetBenchStoreVectorShares(
        const RmvoleNetBenchParams& params,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& senderOut,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& receiverOut,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p0Rows,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p1Rows,
        bool useSRows,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        for (u64 j = 0; j < params.N; ++j)
        {
            for (u64 h = 0; h < M; ++h)
            {
                u64 zero = 0;
                u64 negSender;
                scalarCtx.minus(negSender, zero, senderOut[j][h]);
                auto& p0 = useSRows ? p0Rows[h].V : p0Rows[h].U;
                auto& p1 = useSRows ? p1Rows[h].W0 : p1Rows[h].W1;
                p0[j] = negSender;
                p1[j] = receiverOut[j][h];
            }
        }
    }

    template<u64 M>
    RmvoleNetBenchSplitShares<M> rmvoleNetBenchBuildSimulatedSplitShares(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        bool useSRows,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        RmvoleNetBenchSplitShares<M> shares;
        vectorCtx.resize(shares.p0Additive, params.t);
        vectorCtx.resize(shares.p1ProgrammedNeg, params.t);
        auto& sparse = useSRows ? instance.sparseS : instance.sparseE;

        for (u64 i = 0; i < params.t; ++i)
        {
            for (u64 h = 0; h < M; ++h)
            {
                u64 product;
                u64 b;
                u64 negB;
                scalarCtx.mul(product, instance.delta[h], sparse.values[i]);
                scalarCtx.fromBlock(b, prng.get<block>());
                scalarCtx.plus(shares.p0Additive[i][h], b, product);
                scalarCtx.minus(negB, static_cast<u64>(0), b);
                shares.p1ProgrammedNeg[i][h] = negB;
            }
        }

        return shares;
    }

    template<u64 M>
    struct RmvoleNetBenchSplitSharePair
    {
        RmvoleNetBenchSplitShares<M> s;
        RmvoleNetBenchSplitShares<M> e;
    };

    template<u64 M>
    typename CoeffCtxPrimeArray64<M>::template Vec<u64> rmvoleNetBenchBuildCombinedSparseCoefficients(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        CoeffCtxPrimeArray64<M>& vectorCtx)
    {
        typename CoeffCtxPrimeArray64<M>::template Vec<u64> c;
        vectorCtx.resize(c, 2 * params.t);
        for (u64 i = 0; i < params.t; ++i)
        {
            c[i] = instance.sparseS.values[i];
            c[params.t + i] = instance.sparseE.values[i];
        }
        return c;
    }

    template<u64 M>
    void rmvoleNetBenchSplitNoisyVoleOutputs(
        const RmvoleNetBenchParams& params,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& a,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& b,
        RmvoleNetBenchSplitSharePair<M>& out,
        CoeffCtxPrimeArray64<M>& vectorCtx)
    {
        vectorCtx.resize(out.s.p0Additive, params.t);
        vectorCtx.resize(out.s.p1ProgrammedNeg, params.t);
        vectorCtx.resize(out.e.p0Additive, params.t);
        vectorCtx.resize(out.e.p1ProgrammedNeg, params.t);

        for (u64 i = 0; i < params.t; ++i)
        {
            vectorCtx.copy(out.s.p0Additive[i], a[i]);
            vectorCtx.minus(out.s.p1ProgrammedNeg[i], RmvolePprfPrimeVector<M>{}, b[i]);
            vectorCtx.copy(out.e.p0Additive[i], a[params.t + i]);
            vectorCtx.minus(out.e.p1ProgrammedNeg[i], RmvolePprfPrimeVector<M>{}, b[params.t + i]);
        }
    }

    template<u64 M>
    RmvoleNetBenchSplitSharePair<M> rmvoleNetBenchRunLocalNoisyVoleSplitInput(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        cp::Socket& p0ReceiverSocket,
        cp::Socket& p1SenderSocket,
        double& splitSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;
        using VecG = typename CoeffCtxPrimeArray64<M>::template Vec<u64>;

        NoisyVoleReceiver<VectorF, u64, CoeffCtxPrimeArray64<M>> receiver;
        NoisyVoleSender<VectorF, u64, CoeffCtxPrimeArray64<M>> sender;
        DefaultBaseOT receiverBaseOt;
        DefaultBaseOT senderBaseOt;
        VecG c = rmvoleNetBenchBuildCombinedSparseCoefficients<M>(params, instance, vectorCtx);
        VecF a;
        VecF b;
        vectorCtx.resize(a, 2 * params.t);
        vectorCtx.resize(b, 2 * params.t);

        auto start = omp_get_wtime();
        auto recvTask = receiver.receive(c, a, prng, receiverBaseOt, p0ReceiverSocket, vectorCtx);
        auto sendTask = sender.send(instance.delta, b, prng, senderBaseOt, p1SenderSocket, vectorCtx);
        auto result = macoro::sync_wait(macoro::when_all_ready(std::move(recvTask), std::move(sendTask)));
        std::get<0>(result).result();
        std::get<1>(result).result();
        splitSeconds += omp_get_wtime() - start;

        RmvoleNetBenchSplitSharePair<M> out;
        rmvoleNetBenchSplitNoisyVoleOutputs<M>(params, a, b, out, vectorCtx);
        return out;
    }

    template<u64 M>
    RmvoleNetBenchSplitSharePair<M> rmvoleNetBenchRunTcpServerNoisyVoleSplitInput(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        cp::Socket& socket,
        double& splitSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;
        using VecG = typename CoeffCtxPrimeArray64<M>::template Vec<u64>;

        NoisyVoleReceiver<VectorF, u64, CoeffCtxPrimeArray64<M>> receiver;
        DefaultBaseOT receiverBaseOt;
        VecG c = rmvoleNetBenchBuildCombinedSparseCoefficients<M>(params, instance, vectorCtx);
        VecF a;
        vectorCtx.resize(a, 2 * params.t);

        auto start = omp_get_wtime();
        auto recvTask = receiver.receive(c, a, prng, receiverBaseOt, socket, vectorCtx);
        macoro::sync_wait(std::move(recvTask));
        splitSeconds += omp_get_wtime() - start;

        RmvoleNetBenchSplitSharePair<M> out;
        vectorCtx.resize(out.s.p0Additive, params.t);
        vectorCtx.resize(out.s.p1ProgrammedNeg, params.t);
        vectorCtx.resize(out.e.p0Additive, params.t);
        vectorCtx.resize(out.e.p1ProgrammedNeg, params.t);
        for (u64 i = 0; i < params.t; ++i)
        {
            vectorCtx.copy(out.s.p0Additive[i], a[i]);
            vectorCtx.copy(out.e.p0Additive[i], a[params.t + i]);
        }
        return out;
    }

    template<u64 M>
    RmvoleNetBenchSplitSharePair<M> rmvoleNetBenchRunTcpClientNoisyVoleSplitInput(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        cp::Socket& socket,
        double& splitSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;

        NoisyVoleSender<VectorF, u64, CoeffCtxPrimeArray64<M>> sender;
        DefaultBaseOT senderBaseOt;
        VecF b;
        vectorCtx.resize(b, 2 * params.t);

        auto start = omp_get_wtime();
        auto sendTask = sender.send(instance.delta, b, prng, senderBaseOt, socket, vectorCtx);
        macoro::sync_wait(std::move(sendTask));
        splitSeconds += omp_get_wtime() - start;

        RmvoleNetBenchSplitSharePair<M> out;
        vectorCtx.resize(out.s.p0Additive, params.t);
        vectorCtx.resize(out.s.p1ProgrammedNeg, params.t);
        vectorCtx.resize(out.e.p0Additive, params.t);
        vectorCtx.resize(out.e.p1ProgrammedNeg, params.t);
        for (u64 i = 0; i < params.t; ++i)
        {
            vectorCtx.minus(out.s.p1ProgrammedNeg[i], VectorF{}, b[i]);
            vectorCtx.minus(out.e.p1ProgrammedNeg[i], VectorF{}, b[params.t + i]);
        }
        return out;
    }

    template<u64 M>
    std::vector<RmvolePprfPrimeVector<M>> rmvoleNetBenchToWireVector(
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& values)
    {
        std::vector<RmvolePprfPrimeVector<M>> out(values.size());
        for (u64 i = 0; i < static_cast<u64>(values.size()); ++i)
            out[i] = values[i];
        return out;
    }

    template<u64 M>
    typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>> rmvoleNetBenchFromWireVector(
        const std::vector<RmvolePprfPrimeVector<M>>& values,
        CoeffCtxPrimeArray64<M>& vectorCtx)
    {
        typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>> out;
        vectorCtx.resize(out, values.size());
        for (u64 i = 0; i < static_cast<u64>(values.size()); ++i)
            out[i] = values[i];
        return out;
    }

    template<u64 M>
    void rmvoleNetBenchStoreSplitVectorShares(
        const RmvoleNetBenchParams& params,
        const RmvolePprfSparseInput<u64>& sparse,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& p0Additive,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& senderOut,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& receiverOut,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p0Rows,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p1Rows,
        bool useSRows,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        for (u64 j = 0; j < params.N; ++j)
        {
            for (u64 h = 0; h < M; ++h)
            {
                u64 negSender;
                scalarCtx.minus(negSender, static_cast<u64>(0), senderOut[j][h]);
                auto& p0 = useSRows ? p0Rows[h].V : p0Rows[h].U;
                auto& p1 = useSRows ? p1Rows[h].W0 : p1Rows[h].W1;
                p0[j] = receiverOut[j][h];
                p1[j] = negSender;
            }
        }

        for (u64 i = 0; i < params.t; ++i)
        {
            auto idx = i * params.blockSize + sparse.offsets[i];
            for (u64 h = 0; h < M; ++h)
            {
                auto& p0 = useSRows ? p0Rows[h].V : p0Rows[h].U;
                scalarCtx.plus(p0[idx], p0[idx], p0Additive[i][h]);
            }
        }
    }

    template<u64 M>
    void rmvoleNetBenchStoreSplitP0Rows(
        const RmvoleNetBenchParams& params,
        const RmvolePprfSparseInput<u64>& sparse,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& p0Additive,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& receiverOut,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p0Rows,
        bool useSRows,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        for (u64 j = 0; j < params.N; ++j)
        {
            for (u64 h = 0; h < M; ++h)
            {
                auto& p0 = useSRows ? p0Rows[h].V : p0Rows[h].U;
                p0[j] = receiverOut[j][h];
            }
        }

        for (u64 i = 0; i < params.t; ++i)
        {
            auto idx = i * params.blockSize + sparse.offsets[i];
            for (u64 h = 0; h < M; ++h)
            {
                auto& p0 = useSRows ? p0Rows[h].V : p0Rows[h].U;
                scalarCtx.plus(p0[idx], p0[idx], p0Additive[i][h]);
            }
        }
    }

    template<u64 M>
    void rmvoleNetBenchStoreSplitP1Rows(
        const RmvoleNetBenchParams& params,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& senderOut,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p1Rows,
        bool useSRows,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        for (u64 j = 0; j < params.N; ++j)
        {
            for (u64 h = 0; h < M; ++h)
            {
                u64 negSender;
                scalarCtx.minus(negSender, static_cast<u64>(0), senderOut[j][h]);
                auto& p1 = useSRows ? p1Rows[h].W0 : p1Rows[h].W1;
                p1[j] = negSender;
            }
        }
    }

    template<u64 M>
    bool rmvoleNetBenchLocalVectorPprf(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p0Rows,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p1Rows,
        bool useSRows,
        cp::Socket& senderSocket,
        cp::Socket& receiverSocket,
        double& setupSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;

        RegularPprfSender<VectorF, VectorF, CoeffCtxPrimeArray64<M>> sender;
        RegularPprfReceiver<VectorF, VectorF, CoeffCtxPrimeArray64<M>> receiver;
        VecF senderOut, receiverOut;

        auto& sparse = useSRows ? instance.sparseS : instance.sparseE;
        auto start = omp_get_wtime();
        sender.configure(params.blockSize, params.t);
        receiver.configure(params.blockSize, params.t);
        auto choices = rmvolePprfChoiceBitsFromOffsets(sparse.offsets, receiver.mDepth);
        receiver.setChoiceBits(choices);
        auto beta = rmvolePprfBuildVectorBeta<M>(
            RmvolePprfNetSetupParams{params.logN, params.N, params.t, params.m, params.blockSize},
            instance.delta,
            sparse,
            vectorCtx,
            scalarCtx);

        std::vector<std::array<block, 2>> senderBaseOts(sender.baseOtCount());
        std::vector<block> receiverBaseOts(receiver.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT senderBaseOt;
        DefaultBaseOT receiverBaseOt;
        auto sendBase = senderBaseOt.send(senderBaseOts, prng, senderSocket);
        auto recvBase = receiverBaseOt.receive(choices, receiverBaseOts, prng, receiverSocket);
        macoro::sync_wait(macoro::when_all_ready(std::move(sendBase), std::move(recvBase)));
#else
        throw std::runtime_error("RMVOLE_NET_BENCH requires libOTe base OT support.");
#endif
        sender.setBase(senderBaseOts);
        receiver.setBase(receiverBaseOts);
        vectorCtx.resize(senderOut, params.N);
        vectorCtx.resize(receiverOut, params.N);

        auto sendPprf = sender.expand(senderSocket, beta, prng.get<block>(), senderOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        auto recvPprf = receiver.expand(receiverSocket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        macoro::sync_wait(macoro::when_all_ready(std::move(sendPprf), std::move(recvPprf)));
        setupSeconds += omp_get_wtime() - start;

        rmvoleNetBenchStoreVectorShares<M>(params, senderOut, receiverOut, p0Rows, p1Rows, useSRows, scalarCtx);
        return true;
    }

    template<u64 M>
    bool rmvoleNetBenchLocalSplitInputVectorPprf(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        const RmvoleNetBenchSplitShares<M>& splitShares,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p0Rows,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p1Rows,
        bool useSRows,
        cp::Socket& p0ReceiverSocket,
        cp::Socket& p1SenderSocket,
        double& pprfSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;

        RegularPprfSender<VectorF, VectorF, CoeffCtxPrimeArray64<M>> sender;
        RegularPprfReceiver<VectorF, VectorF, CoeffCtxPrimeArray64<M>> receiver;
        VecF senderOut, receiverOut;
        auto& sparse = useSRows ? instance.sparseS : instance.sparseE;

        auto start = omp_get_wtime();
        sender.configure(params.blockSize, params.t);
        receiver.configure(params.blockSize, params.t);
        auto choices = rmvolePprfChoiceBitsFromOffsets(sparse.offsets, receiver.mDepth);
        receiver.setChoiceBits(choices);

        std::vector<std::array<block, 2>> senderBaseOts(sender.baseOtCount());
        std::vector<block> receiverBaseOts(receiver.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT senderBaseOt;
        DefaultBaseOT receiverBaseOt;
        auto sendBase = senderBaseOt.send(senderBaseOts, prng, p1SenderSocket);
        auto recvBase = receiverBaseOt.receive(choices, receiverBaseOts, prng, p0ReceiverSocket);
        macoro::sync_wait(macoro::when_all_ready(std::move(sendBase), std::move(recvBase)));
#else
        throw std::runtime_error("RMVOLE_NET_BENCH requires libOTe base OT support.");
#endif
        sender.setBase(senderBaseOts);
        receiver.setBase(receiverBaseOts);
        vectorCtx.resize(senderOut, params.N);
        vectorCtx.resize(receiverOut, params.N);

        auto sendPprf = sender.expand(p1SenderSocket, splitShares.p1ProgrammedNeg, prng.get<block>(), senderOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        auto recvPprf = receiver.expand(p0ReceiverSocket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        macoro::sync_wait(macoro::when_all_ready(std::move(sendPprf), std::move(recvPprf)));
        pprfSeconds += omp_get_wtime() - start;

        rmvoleNetBenchStoreSplitVectorShares<M>(
            params,
            sparse,
            splitShares.p0Additive,
            senderOut,
            receiverOut,
            p0Rows,
            p1Rows,
            useSRows,
            scalarCtx);
        return true;
    }

    template<u64 M>
    void rmvoleNetBenchTcpServerVectorPprf(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p0Rows,
        bool useSRows,
        cp::Socket& socket,
        double& setupSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;

        RegularPprfSender<VectorF, VectorF, CoeffCtxPrimeArray64<M>> sender;
        VecF senderOut;
        auto& sparse = useSRows ? instance.sparseS : instance.sparseE;

        auto start = omp_get_wtime();
        sender.configure(params.blockSize, params.t);
        auto beta = rmvolePprfBuildVectorBeta<M>(
            RmvolePprfNetSetupParams{params.logN, params.N, params.t, params.m, params.blockSize},
            instance.delta,
            sparse,
            vectorCtx,
            scalarCtx);

        std::vector<std::array<block, 2>> senderBaseOts(sender.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT senderBaseOt;
        auto sendBase = senderBaseOt.send(senderBaseOts, prng, socket);
        macoro::sync_wait(std::move(sendBase));
#else
        throw std::runtime_error("RMVOLE_NET_BENCH requires libOTe base OT support.");
#endif
        sender.setBase(senderBaseOts);
        vectorCtx.resize(senderOut, params.N);
        auto sendPprf = sender.expand(socket, beta, prng.get<block>(), senderOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        macoro::sync_wait(std::move(sendPprf));
        setupSeconds += omp_get_wtime() - start;

        for (u64 j = 0; j < params.N; ++j)
        {
            for (u64 h = 0; h < M; ++h)
            {
                u64 zero = 0;
                u64 negSender;
                scalarCtx.minus(negSender, zero, senderOut[j][h]);
                auto& p0 = useSRows ? p0Rows[h].V : p0Rows[h].U;
                p0[j] = negSender;
            }
        }
    }

    template<u64 M>
    void rmvoleNetBenchTcpClientVectorPprf(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p1Rows,
        bool useSRows,
        cp::Socket& socket,
        double& setupSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;

        RegularPprfReceiver<VectorF, VectorF, CoeffCtxPrimeArray64<M>> receiver;
        VecF receiverOut;
        auto& sparse = useSRows ? instance.sparseS : instance.sparseE;

        auto start = omp_get_wtime();
        receiver.configure(params.blockSize, params.t);
        auto choices = rmvolePprfChoiceBitsFromOffsets(sparse.offsets, receiver.mDepth);
        receiver.setChoiceBits(choices);

        std::vector<block> receiverBaseOts(receiver.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT receiverBaseOt;
        auto recvBase = receiverBaseOt.receive(choices, receiverBaseOts, prng, socket);
        macoro::sync_wait(std::move(recvBase));
#else
        throw std::runtime_error("RMVOLE_NET_BENCH requires libOTe base OT support.");
#endif
        receiver.setBase(receiverBaseOts);
        vectorCtx.resize(receiverOut, params.N);
        auto recvPprf = receiver.expand(socket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        macoro::sync_wait(std::move(recvPprf));
        setupSeconds += omp_get_wtime() - start;

        for (u64 j = 0; j < params.N; ++j)
        {
            for (u64 h = 0; h < M; ++h)
            {
                auto& p1 = useSRows ? p1Rows[h].W0 : p1Rows[h].W1;
                p1[j] = receiverOut[j][h];
            }
        }
    }

    template<u64 M>
    void rmvoleNetBenchTcpServerSplitInputP0Pprf(
        const RmvoleNetBenchParams& params,
        const RmvoleNetBenchInstance<M>& instance,
        const RmvoleNetBenchSplitShares<M>& splitShares,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p0Rows,
        bool useSRows,
        cp::Socket& socket,
        double& pprfSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;

        RegularPprfReceiver<VectorF, VectorF, CoeffCtxPrimeArray64<M>> receiver;
        VecF receiverOut;
        auto& sparse = useSRows ? instance.sparseS : instance.sparseE;

        auto start = omp_get_wtime();
        receiver.configure(params.blockSize, params.t);
        auto choices = rmvolePprfChoiceBitsFromOffsets(sparse.offsets, receiver.mDepth);
        receiver.setChoiceBits(choices);

        std::vector<block> receiverBaseOts(receiver.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT receiverBaseOt;
        auto recvBase = receiverBaseOt.receive(choices, receiverBaseOts, prng, socket);
        macoro::sync_wait(std::move(recvBase));
#else
        throw std::runtime_error("RMVOLE_NET_BENCH requires libOTe base OT support.");
#endif
        receiver.setBase(receiverBaseOts);
        vectorCtx.resize(receiverOut, params.N);
        auto recvPprf = receiver.expand(socket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        macoro::sync_wait(std::move(recvPprf));
        pprfSeconds += omp_get_wtime() - start;

        rmvoleNetBenchStoreSplitP0Rows<M>(
            params,
            sparse,
            splitShares.p0Additive,
            receiverOut,
            p0Rows,
            useSRows,
            scalarCtx);
    }

    template<u64 M>
    void rmvoleNetBenchTcpClientSplitInputP1Pprf(
        const RmvoleNetBenchParams& params,
        const typename CoeffCtxPrimeArray64<M>::template Vec<RmvolePprfPrimeVector<M>>& p1ProgrammedNeg,
        std::vector<ModuleMVOLERowMask<u64, CoeffCtxIntegerPrime_64>>& p1Rows,
        bool useSRows,
        cp::Socket& socket,
        double& pprfSeconds,
        PRNG& prng,
        CoeffCtxPrimeArray64<M>& vectorCtx,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        using VectorF = RmvolePprfPrimeVector<M>;
        using VecF = typename CoeffCtxPrimeArray64<M>::template Vec<VectorF>;

        RegularPprfSender<VectorF, VectorF, CoeffCtxPrimeArray64<M>> sender;
        VecF senderOut;

        auto start = omp_get_wtime();
        sender.configure(params.blockSize, params.t);

        std::vector<std::array<block, 2>> senderBaseOts(sender.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT senderBaseOt;
        auto sendBase = senderBaseOt.send(senderBaseOts, prng, socket);
        macoro::sync_wait(std::move(sendBase));
#else
        throw std::runtime_error("RMVOLE_NET_BENCH requires libOTe base OT support.");
#endif
        sender.setBase(senderBaseOts);
        vectorCtx.resize(senderOut, params.N);
        auto sendPprf = sender.expand(socket, p1ProgrammedNeg, prng.get<block>(), senderOut, PprfOutputFormat::ByTreeIndex, true, 1, vectorCtx);
        macoro::sync_wait(std::move(sendPprf));
        pprfSeconds += omp_get_wtime() - start;

        rmvoleNetBenchStoreSplitP1Rows<M>(
            params,
            senderOut,
            p1Rows,
            useSRows,
            scalarCtx);
    }

    template<u64 M>
    RmvoleNetBenchRow rmvoleNetBenchRunLocalTyped(
        const RmvoleNetBenchParams& params,
        RmvoleNetBenchMode mode,
        RmvoleNetBenchSetupMode setupMode)
    {
        RmvoleNetBenchRow row;
        if (setupMode == RmvoleNetBenchSetupMode::SplitInputVole)
            row.method = "RMVOLE-noisy-VOLE-split-input-vector-RegularPprf-setup-local-expand";
        else if (setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
            row.method = "RMVOLE-simulated-split-input-vector-RegularPprf-setup-local-expand";
        else
            row.method = "RMVOLE-vector-RegularPprf-setup-local-expand";
        row.mode = rmvoleNetBenchModeName(mode);
        row.setupMode = rmvoleNetBenchSetupModeName(setupMode);
        row.logN = params.logN;
        row.N = params.N;
        row.t = params.t;
        row.m = params.m;
        row.blockSize = params.blockSize;
        row.entries = params.m * params.N;
        if (setupMode == RmvoleNetBenchSetupMode::SplitInputVole)
            row.notes = "local_async_pair; real_libOTe_NoisyVole_split_input_length_2t; F=array<u64,M>; G=u64; p0_noisy_vole_receiver; p1_noisy_vole_sender; vector_fixed_prime_setup; local_expand";
        else if (setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
            row.notes = "local_async_pair; simulated_split_input_product_shares; p0_receiver_p1_sender; vector_fixed_prime_setup; local_expand; not_secure_split_input";
        else
            row.notes = "local_async_pair; vector_fixed_prime_setup; local_expand; centralized_test_instance";

        PRNG prng(sysRandomSeed());
        CoeffCtxIntegerPrime_64 scalarCtx;
        CoeffCtxPrimeArray64<M> vectorCtx;
        auto instance = rmvoleNetBenchSampleInstance<M>(params, prng);
        ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p0Gen, p1Gen;
        ModuleMVOLEResult<u64, CoeffCtxIntegerPrime_64> result;

        rmvoleNetBenchBuildP0Gen<M>(p0Gen, params, instance, scalarCtx);
        rmvoleNetBenchBuildP1Gen<M>(p1Gen, params, instance, scalarCtx);

        auto sockets = cp::LocalAsyncSocket::makePair();
        if (setupMode == RmvoleNetBenchSetupMode::SplitInputVole)
        {
            auto before = sockets[0].bytesSent() + sockets[1].bytesSent();
            auto shares = rmvoleNetBenchRunLocalNoisyVoleSplitInput<M>(params, instance, sockets[0], sockets[1], row.splitInputSeconds, prng, vectorCtx);
            macoro::sync_wait(macoro::when_all_ready(sockets[0].flush(), sockets[1].flush()));
            auto after = sockets[0].bytesSent() + sockets[1].bytesSent();
            row.splitInputBytes += after >= before ? after - before : 0;

            before = sockets[0].bytesSent() + sockets[1].bytesSent();
            rmvoleNetBenchLocalSplitInputVectorPprf<M>(params, instance, shares.s, p0Gen.rowMasks, p1Gen.rowMasks, true, sockets[0], sockets[1], row.pprfSetupSeconds, prng, vectorCtx, scalarCtx);
            rmvoleNetBenchLocalSplitInputVectorPprf<M>(params, instance, shares.e, p0Gen.rowMasks, p1Gen.rowMasks, false, sockets[0], sockets[1], row.pprfSetupSeconds, prng, vectorCtx, scalarCtx);
            after = sockets[0].bytesSent() + sockets[1].bytesSent();
            row.pprfBytes += after >= before ? after - before : 0;
            row.setupSeconds = row.splitInputSeconds + row.pprfSetupSeconds;
        }
        else if (setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
        {
            auto start = omp_get_wtime();
            auto sShares = rmvoleNetBenchBuildSimulatedSplitShares<M>(params, instance, true, prng, vectorCtx, scalarCtx);
            row.splitInputSeconds += omp_get_wtime() - start;
            row.splitInputBytes += rmvoleNetBenchSplitShareBytes(params);
            rmvoleNetBenchLocalSplitInputVectorPprf<M>(params, instance, sShares, p0Gen.rowMasks, p1Gen.rowMasks, true, sockets[0], sockets[1], row.pprfSetupSeconds, prng, vectorCtx, scalarCtx);

            start = omp_get_wtime();
            auto eShares = rmvoleNetBenchBuildSimulatedSplitShares<M>(params, instance, false, prng, vectorCtx, scalarCtx);
            row.splitInputSeconds += omp_get_wtime() - start;
            row.splitInputBytes += rmvoleNetBenchSplitShareBytes(params);
            rmvoleNetBenchLocalSplitInputVectorPprf<M>(params, instance, eShares, p0Gen.rowMasks, p1Gen.rowMasks, false, sockets[0], sockets[1], row.pprfSetupSeconds, prng, vectorCtx, scalarCtx);
            row.setupSeconds = row.splitInputSeconds + row.pprfSetupSeconds;
        }
        else
        {
            auto before = sockets[0].bytesSent() + sockets[1].bytesSent();
            rmvoleNetBenchLocalVectorPprf<M>(params, instance, p0Gen.rowMasks, p1Gen.rowMasks, true, sockets[0], sockets[1], row.pprfSetupSeconds, prng, vectorCtx, scalarCtx);
            rmvoleNetBenchLocalVectorPprf<M>(params, instance, p0Gen.rowMasks, p1Gen.rowMasks, false, sockets[0], sockets[1], row.pprfSetupSeconds, prng, vectorCtx, scalarCtx);
            auto after = sockets[0].bytesSent() + sockets[1].bytesSent();
            row.pprfBytes += after >= before ? after - before : 0;
            row.setupSeconds = row.pprfSetupSeconds;
        }

        auto start = omp_get_wtime();
        moduleMvoleExpandP0<u64, CoeffCtxIntegerPrime_64>(result.p0, rmvoleNetBenchModuleParams(params), p0Gen, scalarCtx);
        row.expandP0Seconds = omp_get_wtime() - start;

        start = omp_get_wtime();
        moduleMvoleExpandP1<u64, CoeffCtxIntegerPrime_64>(result.p1, rmvoleNetBenchModuleParams(params), p1Gen, scalarCtx);
        row.expandP1Seconds = omp_get_wtime() - start;

        if (mode == RmvoleNetBenchMode::Verify)
        {
            start = omp_get_wtime();
            row.ok = moduleMvoleVerify<u64, CoeffCtxIntegerPrime_64>(result, rmvoleNetBenchModuleParams(params), scalarCtx, &std::cout);
            row.verifySeconds = omp_get_wtime() - start;
        }
        else
        {
            row.ok = true;
        }

        row.bytesSentByRole = sockets[0].bytesSent();
        row.bytesReceivedByRole = sockets[1].bytesSent();
        row.totalSocketBytes = row.bytesSentByRole + row.bytesReceivedByRole;
        if (setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
            row.pprfBytes = row.totalSocketBytes;
        row.cleanProtocolBytes = setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated
            ? row.totalSocketBytes + row.splitInputBytes
            : row.totalSocketBytes;
        row.totalSeconds = row.setupSeconds + std::max(row.expandP0Seconds, row.expandP1Seconds);
        row.entriesPerSecond = row.totalSeconds > 0 ? static_cast<double>(row.entries) / row.totalSeconds : 0;
        row.bytesPerEntry = row.entries ? static_cast<double>(row.cleanProtocolBytes) / static_cast<double>(row.entries) : 0;
        return row;
    }

    template<u64 M>
    void rmvoleNetBenchSendInstanceMetadata(
        cp::Socket& socket,
        const RmvoleNetBenchInstance<M>& instance)
    {
        macoro::sync_wait(socket.send(coproto::copy(instance.rhoSeed)));
        macoro::sync_wait(socket.send(coproto::copy(instance.delta)));
        macoro::sync_wait(socket.send(coproto::copy(instance.sparseS.offsets)));
        macoro::sync_wait(socket.send(coproto::copy(instance.sparseE.offsets)));
    }

    template<u64 M>
    void rmvoleNetBenchSendSplitInputMetadata(
        cp::Socket& socket,
        const RmvoleNetBenchInstance<M>& instance)
    {
        macoro::sync_wait(socket.send(coproto::copy(instance.rhoSeed)));
        macoro::sync_wait(socket.send(coproto::copy(instance.delta)));
    }

    template<u64 M>
    RmvoleNetBenchInstance<M> rmvoleNetBenchRecvInstanceMetadata(
        cp::Socket& socket,
        const RmvoleNetBenchParams& params)
    {
        RmvoleNetBenchInstance<M> instance;
        instance.sparseS.offsets.resize(params.t);
        instance.sparseE.offsets.resize(params.t);
        macoro::sync_wait(socket.recv(instance.rhoSeed));
        macoro::sync_wait(socket.recv(instance.delta));
        macoro::sync_wait(socket.recv(instance.sparseS.offsets));
        macoro::sync_wait(socket.recv(instance.sparseE.offsets));
        instance.sparseS.values.resize(params.t);
        instance.sparseE.values.resize(params.t);
        return instance;
    }

    template<u64 M>
    RmvoleNetBenchInstance<M> rmvoleNetBenchRecvSplitInputMetadata(
        cp::Socket& socket,
        const RmvoleNetBenchParams& params)
    {
        RmvoleNetBenchInstance<M> instance;
        macoro::sync_wait(socket.recv(instance.rhoSeed));
        macoro::sync_wait(socket.recv(instance.delta));
        instance.sparseS.values.resize(params.t);
        instance.sparseE.values.resize(params.t);
        instance.sparseS.offsets.resize(params.t);
        instance.sparseE.offsets.resize(params.t);
        return instance;
    }

    inline u64 rmvoleNetBenchMetadataBytes(const RmvoleNetBenchParams& params)
    {
        return sizeof(block) + params.m * sizeof(u64) + 2 * params.t * sizeof(u64);
    }

    inline u64 rmvoleNetBenchOpeningBytes(const RmvoleNetBenchParams& params)
    {
        return params.N * sizeof(u64) + params.m * params.N * sizeof(u64);
    }

    template<u64 M>
    RmvoleNetBenchRow rmvoleNetBenchRunTcpRoleTyped(
        bool server,
        const std::string& host,
        const std::string& port,
        const RmvoleNetBenchParams& params,
        u64 reps,
        RmvoleNetBenchMode mode,
        RmvoleNetBenchSetupMode setupMode)
    {
        auto address = host + ":" + port;
        auto socket = cp::asioConnect(address, server);
        PRNG prng(sysRandomSeed());
        CoeffCtxIntegerPrime_64 scalarCtx;
        CoeffCtxPrimeArray64<M> vectorCtx;
        std::vector<double> splitTimes, pprfTimes, setupTimes, expandP0Times, expandP1Times, totalTimes, verifyTimes;
        bool ok = true;

        RmvoleNetBenchRow row;
        if (setupMode == RmvoleNetBenchSetupMode::SplitInputVole)
            row.method = "RMVOLE-noisy-VOLE-split-input-vector-RegularPprf-setup-local-expand";
        else if (setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
            row.method = "RMVOLE-simulated-split-input-vector-RegularPprf-setup-local-expand";
        else
            row.method = "RMVOLE-vector-RegularPprf-setup-local-expand";
        row.mode = rmvoleNetBenchModeName(mode);
        row.setupMode = rmvoleNetBenchSetupModeName(setupMode);
        if (rmvoleNetBenchUsesSplitInput(setupMode))
            row.role = server ? "receiver-server-p0" : "sender-client-p1";
        else
            row.role = server ? "sender-server-p0" : "receiver-client-p1";
        row.network = rmvoleNetBenchTcpNetworkLabel(host);
        row.address = address;
        row.logN = params.logN;
        row.N = params.N;
        row.t = params.t;
        row.m = params.m;
        row.blockSize = params.blockSize;
        row.reps = reps;
        row.entries = params.m * params.N;
        if (setupMode == RmvoleNetBenchSetupMode::SplitInputVole)
        {
            row.notes = server
                ? "tcp_two_process; p0_noisy_vole_receiver; real_libOTe_NoisyVole_split_input_length_2t; F=array<u64,M>; G=u64; vector_fixed_prime_setup; local_expand"
                : "tcp_two_process; p1_noisy_vole_sender; real_libOTe_NoisyVole_split_input_length_2t; F=array<u64,M>; G=u64; vector_fixed_prime_setup; local_expand";
        }
        else if (setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
        {
            row.notes = server
                ? "tcp_two_process; p0_receiver; simulated_split_input_product_shares; sends_neg_B_payload_shares; vector_fixed_prime_setup; local_expand; not_secure_split_input"
                : "tcp_two_process; p1_sender; receives_simulated_neg_B_payload_shares; vector_fixed_prime_setup; local_expand; not_secure_split_input";
        }
        else
        {
            row.notes = server
                ? "tcp_two_process; p0_sender; vector_fixed_prime_setup; local_expand; centralized_test_instance"
                : "tcp_two_process; p1_receiver; vector_fixed_prime_setup; local_expand; centralized_test_instance";
        }

        for (u64 rep = 0; rep < reps; ++rep)
        {
            double splitInput = 0;
            double pprfSetup = 0;
            double setup = 0;
            double expandP0 = 0;
            double expandP1 = 0;
            double verify = 0;

            if (server && setupMode == RmvoleNetBenchSetupMode::Centralized)
            {
                auto instance = rmvoleNetBenchSampleInstance<M>(params, prng);
                rmvoleNetBenchSendInstanceMetadata<M>(socket, instance);
                row.harnessMetadataBytes += rmvoleNetBenchMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p0Gen;
                ModuleMVOLEP0Output<u64, CoeffCtxIntegerPrime_64> p0;
                rmvoleNetBenchBuildP0Gen<M>(p0Gen, params, instance, scalarCtx);
                auto before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpServerVectorPprf<M>(params, instance, p0Gen.rowMasks, true, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                rmvoleNetBenchTcpServerVectorPprf<M>(params, instance, p0Gen.rowMasks, false, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                auto after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;
                setup = pprfSetup;

                auto start = omp_get_wtime();
                moduleMvoleExpandP0<u64, CoeffCtxIntegerPrime_64>(p0, rmvoleNetBenchModuleParams(params), p0Gen, scalarCtx);
                expandP0 = omp_get_wtime() - start;

                if (mode == RmvoleNetBenchMode::Verify)
                {
                    macoro::sync_wait(socket.send(coproto::copy(p0.x)));
                    macoro::sync_wait(socket.send(coproto::copy(p0.Z0)));
                    row.verificationOpeningBytes += rmvoleNetBenchOpeningBytes(params);
                }
            }
            else if (!server && setupMode == RmvoleNetBenchSetupMode::Centralized)
            {
                auto instance = rmvoleNetBenchRecvInstanceMetadata<M>(socket, params);
                row.harnessMetadataBytes += rmvoleNetBenchMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p1Gen;
                ModuleMVOLEP1Output<u64, CoeffCtxIntegerPrime_64> p1;
                rmvoleNetBenchBuildP1Gen<M>(p1Gen, params, instance, scalarCtx);
                auto before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpClientVectorPprf<M>(params, instance, p1Gen.rowMasks, true, socket, pprfSetup, prng, vectorCtx);
                rmvoleNetBenchTcpClientVectorPprf<M>(params, instance, p1Gen.rowMasks, false, socket, pprfSetup, prng, vectorCtx);
                auto after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;
                setup = pprfSetup;

                auto start = omp_get_wtime();
                moduleMvoleExpandP1<u64, CoeffCtxIntegerPrime_64>(p1, rmvoleNetBenchModuleParams(params), p1Gen, scalarCtx);
                expandP1 = omp_get_wtime() - start;

                if (mode == RmvoleNetBenchMode::Verify)
                {
                    ModuleMVOLEResult<u64, CoeffCtxIntegerPrime_64> result;
                    result.p1 = std::move(p1);
                    macoro::sync_wait(socket.recv(result.p0.x));
                    macoro::sync_wait(socket.recv(result.p0.Z0));
                    row.verificationOpeningBytes += rmvoleNetBenchOpeningBytes(params);

                    start = omp_get_wtime();
                    ok = moduleMvoleVerify<u64, CoeffCtxIntegerPrime_64>(result, rmvoleNetBenchModuleParams(params), scalarCtx, &std::cout);
                    verify = omp_get_wtime() - start;
                }
            }
            else if (server && setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
            {
                auto instance = rmvoleNetBenchSampleInstance<M>(params, prng);
                rmvoleNetBenchSendSplitInputMetadata<M>(socket, instance);
                row.harnessMetadataBytes += rmvoleNetBenchSplitMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p0Gen;
                ModuleMVOLEP0Output<u64, CoeffCtxIntegerPrime_64> p0;
                rmvoleNetBenchBuildP0Gen<M>(p0Gen, params, instance, scalarCtx);

                auto splitStart = omp_get_wtime();
                auto sShares = rmvoleNetBenchBuildSimulatedSplitShares<M>(params, instance, true, prng, vectorCtx, scalarCtx);
                splitInput += omp_get_wtime() - splitStart;
                auto wireS = rmvoleNetBenchToWireVector<M>(sShares.p1ProgrammedNeg);
                macoro::sync_wait(socket.send(coproto::copy(wireS)));
                row.splitInputBytes += rmvoleNetBenchSplitShareBytes(params);

                auto before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpServerSplitInputP0Pprf<M>(params, instance, sShares, p0Gen.rowMasks, true, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                auto after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;

                splitStart = omp_get_wtime();
                auto eShares = rmvoleNetBenchBuildSimulatedSplitShares<M>(params, instance, false, prng, vectorCtx, scalarCtx);
                splitInput += omp_get_wtime() - splitStart;
                auto wireE = rmvoleNetBenchToWireVector<M>(eShares.p1ProgrammedNeg);
                macoro::sync_wait(socket.send(coproto::copy(wireE)));
                row.splitInputBytes += rmvoleNetBenchSplitShareBytes(params);

                before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpServerSplitInputP0Pprf<M>(params, instance, eShares, p0Gen.rowMasks, false, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;
                setup = splitInput + pprfSetup;

                auto start = omp_get_wtime();
                moduleMvoleExpandP0<u64, CoeffCtxIntegerPrime_64>(p0, rmvoleNetBenchModuleParams(params), p0Gen, scalarCtx);
                expandP0 = omp_get_wtime() - start;

                if (mode == RmvoleNetBenchMode::Verify)
                {
                    macoro::sync_wait(socket.send(coproto::copy(p0.x)));
                    macoro::sync_wait(socket.send(coproto::copy(p0.Z0)));
                    row.verificationOpeningBytes += rmvoleNetBenchOpeningBytes(params);
                }
            }
            else if (!server && setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated)
            {
                auto instance = rmvoleNetBenchRecvSplitInputMetadata<M>(socket, params);
                row.harnessMetadataBytes += rmvoleNetBenchSplitMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p1Gen;
                ModuleMVOLEP1Output<u64, CoeffCtxIntegerPrime_64> p1;
                rmvoleNetBenchBuildP1Gen<M>(p1Gen, params, instance, scalarCtx);

                std::vector<RmvolePprfPrimeVector<M>> wireS;
                wireS.resize(params.t);
                auto splitStart = omp_get_wtime();
                macoro::sync_wait(socket.recv(wireS));
                splitInput += omp_get_wtime() - splitStart;
                row.splitInputBytes += rmvoleNetBenchSplitShareBytes(params);
                auto sNeg = rmvoleNetBenchFromWireVector<M>(wireS, vectorCtx);

                auto before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpClientSplitInputP1Pprf<M>(params, sNeg, p1Gen.rowMasks, true, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                auto after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;

                std::vector<RmvolePprfPrimeVector<M>> wireE;
                wireE.resize(params.t);
                splitStart = omp_get_wtime();
                macoro::sync_wait(socket.recv(wireE));
                splitInput += omp_get_wtime() - splitStart;
                row.splitInputBytes += rmvoleNetBenchSplitShareBytes(params);
                auto eNeg = rmvoleNetBenchFromWireVector<M>(wireE, vectorCtx);

                before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpClientSplitInputP1Pprf<M>(params, eNeg, p1Gen.rowMasks, false, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;
                setup = splitInput + pprfSetup;

                auto start = omp_get_wtime();
                moduleMvoleExpandP1<u64, CoeffCtxIntegerPrime_64>(p1, rmvoleNetBenchModuleParams(params), p1Gen, scalarCtx);
                expandP1 = omp_get_wtime() - start;

                if (mode == RmvoleNetBenchMode::Verify)
                {
                    ModuleMVOLEResult<u64, CoeffCtxIntegerPrime_64> result;
                    result.p1 = std::move(p1);
                    macoro::sync_wait(socket.recv(result.p0.x));
                    macoro::sync_wait(socket.recv(result.p0.Z0));
                    row.verificationOpeningBytes += rmvoleNetBenchOpeningBytes(params);

                    auto startVerify = omp_get_wtime();
                    ok = moduleMvoleVerify<u64, CoeffCtxIntegerPrime_64>(result, rmvoleNetBenchModuleParams(params), scalarCtx, &std::cout);
                    verify = omp_get_wtime() - startVerify;
                }
            }
            else if (server)
            {
                auto instance = rmvoleNetBenchSampleInstance<M>(params, prng);
                rmvoleNetBenchSendSplitInputMetadata<M>(socket, instance);
                row.harnessMetadataBytes += rmvoleNetBenchSplitMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p0Gen;
                ModuleMVOLEP0Output<u64, CoeffCtxIntegerPrime_64> p0;
                rmvoleNetBenchBuildP0Gen<M>(p0Gen, params, instance, scalarCtx);

                auto before = socket.bytesSent() + socket.bytesReceived();
                auto shares = rmvoleNetBenchRunTcpServerNoisyVoleSplitInput<M>(params, instance, socket, splitInput, prng, vectorCtx);
                macoro::sync_wait(socket.flush());
                auto after = socket.bytesSent() + socket.bytesReceived();
                row.splitInputBytes += after >= before ? after - before : 0;

                before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpServerSplitInputP0Pprf<M>(params, instance, shares.s, p0Gen.rowMasks, true, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                rmvoleNetBenchTcpServerSplitInputP0Pprf<M>(params, instance, shares.e, p0Gen.rowMasks, false, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;
                setup = splitInput + pprfSetup;

                auto start = omp_get_wtime();
                moduleMvoleExpandP0<u64, CoeffCtxIntegerPrime_64>(p0, rmvoleNetBenchModuleParams(params), p0Gen, scalarCtx);
                expandP0 = omp_get_wtime() - start;

                if (mode == RmvoleNetBenchMode::Verify)
                {
                    macoro::sync_wait(socket.send(coproto::copy(p0.x)));
                    macoro::sync_wait(socket.send(coproto::copy(p0.Z0)));
                    row.verificationOpeningBytes += rmvoleNetBenchOpeningBytes(params);
                }
            }
            else
            {
                auto instance = rmvoleNetBenchRecvSplitInputMetadata<M>(socket, params);
                row.harnessMetadataBytes += rmvoleNetBenchSplitMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p1Gen;
                ModuleMVOLEP1Output<u64, CoeffCtxIntegerPrime_64> p1;
                rmvoleNetBenchBuildP1Gen<M>(p1Gen, params, instance, scalarCtx);

                auto before = socket.bytesSent() + socket.bytesReceived();
                auto shares = rmvoleNetBenchRunTcpClientNoisyVoleSplitInput<M>(params, instance, socket, splitInput, prng, vectorCtx);
                macoro::sync_wait(socket.flush());
                auto after = socket.bytesSent() + socket.bytesReceived();
                row.splitInputBytes += after >= before ? after - before : 0;

                before = socket.bytesSent() + socket.bytesReceived();
                rmvoleNetBenchTcpClientSplitInputP1Pprf<M>(params, shares.s.p1ProgrammedNeg, p1Gen.rowMasks, true, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                rmvoleNetBenchTcpClientSplitInputP1Pprf<M>(params, shares.e.p1ProgrammedNeg, p1Gen.rowMasks, false, socket, pprfSetup, prng, vectorCtx, scalarCtx);
                after = socket.bytesSent() + socket.bytesReceived();
                row.pprfBytes += after >= before ? after - before : 0;
                setup = splitInput + pprfSetup;

                auto start = omp_get_wtime();
                moduleMvoleExpandP1<u64, CoeffCtxIntegerPrime_64>(p1, rmvoleNetBenchModuleParams(params), p1Gen, scalarCtx);
                expandP1 = omp_get_wtime() - start;

                if (mode == RmvoleNetBenchMode::Verify)
                {
                    ModuleMVOLEResult<u64, CoeffCtxIntegerPrime_64> result;
                    result.p1 = std::move(p1);
                    macoro::sync_wait(socket.recv(result.p0.x));
                    macoro::sync_wait(socket.recv(result.p0.Z0));
                    row.verificationOpeningBytes += rmvoleNetBenchOpeningBytes(params);

                    auto startVerify = omp_get_wtime();
                    ok = moduleMvoleVerify<u64, CoeffCtxIntegerPrime_64>(result, rmvoleNetBenchModuleParams(params), scalarCtx, &std::cout);
                    verify = omp_get_wtime() - startVerify;
                }
            }

            splitTimes.push_back(splitInput);
            pprfTimes.push_back(pprfSetup);
            setupTimes.push_back(setup);
            expandP0Times.push_back(expandP0);
            expandP1Times.push_back(expandP1);
            totalTimes.push_back(setup + std::max(expandP0, expandP1));
            verifyTimes.push_back(verify);
            if (!ok)
                break;
        }

        macoro::sync_wait(socket.flush());
        row.splitInputSeconds = rmvoleNetBenchMedian(splitTimes);
        row.pprfSetupSeconds = rmvoleNetBenchMedian(pprfTimes);
        row.setupSeconds = rmvoleNetBenchMedian(setupTimes);
        row.expandP0Seconds = rmvoleNetBenchMedian(expandP0Times);
        row.expandP1Seconds = rmvoleNetBenchMedian(expandP1Times);
        row.totalSeconds = rmvoleNetBenchMedian(totalTimes);
        row.verifySeconds = rmvoleNetBenchMedian(verifyTimes);
        row.bytesSentByRole = socket.bytesSent();
        row.bytesReceivedByRole = socket.bytesReceived();
        row.totalSocketBytes = row.bytesSentByRole + row.bytesReceivedByRole;
        auto excluded = row.harnessMetadataBytes + row.verificationOpeningBytes;
        row.cleanProtocolBytes = row.totalSocketBytes >= excluded ? row.totalSocketBytes - excluded : 0;
        if (setupMode == RmvoleNetBenchSetupMode::SplitInputSimulated ||
            setupMode == RmvoleNetBenchSetupMode::SplitInputVole)
        {
            row.pprfBytes = row.cleanProtocolBytes >= row.splitInputBytes ? row.cleanProtocolBytes - row.splitInputBytes : 0;
        }
        row.entriesPerSecond = row.totalSeconds > 0 ? static_cast<double>(row.entries) / row.totalSeconds : 0;
        row.bytesPerEntry = row.entries ? static_cast<double>(row.cleanProtocolBytes) / static_cast<double>(row.entries) : 0;
        row.ok = ok;
        return row;
    }

    inline void rmvoleNetBenchWriteCsv(const RmvoleNetBenchRow& row)
    {
        std::filesystem::create_directories("docs");
        auto path = std::filesystem::path("docs/rmvole_net_benchmark.csv");
        auto needsHeader = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
        std::ofstream out(path, std::ios::app);
        if (!out)
            throw std::runtime_error("failed to open docs/rmvole_net_benchmark.csv");
        if (needsHeader)
        {
            out << "method,mode,role,network,address,logN,N,t,m,blockSize,reps,"
                << "setup_s,expand_p0_s,expand_p1_s,total_s,verify_s,"
                << "bytes_sent_by_role,bytes_received_by_role,total_socket_bytes,harness_metadata_bytes,verification_opening_bytes,clean_protocol_bytes,"
                << "entries,entries_per_s,bytes_per_entry,pprf_instances,default_base_ot_calls,ok,notes\n";
        }
        out << rmvoleNetBenchCsvEscape(row.method) << ','
            << rmvoleNetBenchCsvEscape(row.mode) << ','
            << rmvoleNetBenchCsvEscape(row.role) << ','
            << rmvoleNetBenchCsvEscape(row.network) << ','
            << rmvoleNetBenchCsvEscape(row.address) << ','
            << row.logN << ','
            << row.N << ','
            << row.t << ','
            << row.m << ','
            << row.blockSize << ','
            << row.reps << ','
            << std::setprecision(12) << row.setupSeconds << ','
            << row.expandP0Seconds << ','
            << row.expandP1Seconds << ','
            << row.totalSeconds << ','
            << row.verifySeconds << ','
            << row.bytesSentByRole << ','
            << row.bytesReceivedByRole << ','
            << row.totalSocketBytes << ','
            << row.harnessMetadataBytes << ','
            << row.verificationOpeningBytes << ','
            << row.cleanProtocolBytes << ','
            << row.entries << ','
            << row.entriesPerSecond << ','
            << row.bytesPerEntry << ','
            << row.pprfInstances << ','
            << row.defaultBaseOtCalls << ','
            << (row.ok ? 1 : 0) << ','
            << rmvoleNetBenchCsvEscape(row.notes) << '\n';
    }

    inline void rmvoleNetBenchWriteSplitInputCsv(const RmvoleNetBenchRow& row)
    {
        std::filesystem::create_directories("docs");
        auto path = std::filesystem::path("docs/rmvole_split_input_benchmark.csv");
        auto needsHeader = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
        std::ofstream out(path, std::ios::app);
        if (!out)
            throw std::runtime_error("failed to open docs/rmvole_split_input_benchmark.csv");
        if (needsHeader)
        {
            out << "method,mode,setup_mode,role,network,address,logN,N,t,m,blockSize,reps,"
                << "split_input_s,pprf_setup_s,setup_s,expand_p0_s,expand_p1_s,total_s,verify_s,"
                << "bytes_sent_by_role,bytes_received_by_role,total_socket_bytes,harness_metadata_bytes,verification_opening_bytes,"
                << "split_input_bytes,pprf_bytes,total_clean_bytes,entries,entries_per_s,bytes_per_entry,"
                << "pprf_instances,default_base_ot_calls,ok,notes\n";
        }
        out << rmvoleNetBenchCsvEscape(row.method) << ','
            << rmvoleNetBenchCsvEscape(row.mode) << ','
            << rmvoleNetBenchCsvEscape(row.setupMode) << ','
            << rmvoleNetBenchCsvEscape(row.role) << ','
            << rmvoleNetBenchCsvEscape(row.network) << ','
            << rmvoleNetBenchCsvEscape(row.address) << ','
            << row.logN << ','
            << row.N << ','
            << row.t << ','
            << row.m << ','
            << row.blockSize << ','
            << row.reps << ','
            << std::setprecision(12) << row.splitInputSeconds << ','
            << row.pprfSetupSeconds << ','
            << row.setupSeconds << ','
            << row.expandP0Seconds << ','
            << row.expandP1Seconds << ','
            << row.totalSeconds << ','
            << row.verifySeconds << ','
            << row.bytesSentByRole << ','
            << row.bytesReceivedByRole << ','
            << row.totalSocketBytes << ','
            << row.harnessMetadataBytes << ','
            << row.verificationOpeningBytes << ','
            << row.splitInputBytes << ','
            << row.pprfBytes << ','
            << row.cleanProtocolBytes << ','
            << row.entries << ','
            << row.entriesPerSecond << ','
            << row.bytesPerEntry << ','
            << row.pprfInstances << ','
            << row.defaultBaseOtCalls << ','
            << (row.ok ? 1 : 0) << ','
            << rmvoleNetBenchCsvEscape(row.notes) << '\n';
    }

    inline void rmvoleNetBenchWriteSplitInputVoleCsv(const RmvoleNetBenchRow& row)
    {
        if (row.setupMode != "split-input-vole")
            return;

        std::filesystem::create_directories("docs");
        auto path = std::filesystem::path("docs/rmvole_split_input_vole_benchmark.csv");
        auto needsHeader = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
        std::ofstream out(path, std::ios::app);
        if (!out)
            throw std::runtime_error("failed to open docs/rmvole_split_input_vole_benchmark.csv");
        if (needsHeader)
        {
            out << "method,mode,setup_mode,role,network,address,logN,N,t,m,blockSize,reps,"
                << "split_input_vole_s,split_input_vole_bytes,pprf_setup_s,pprf_bytes,"
                << "expand_p0_s,expand_p1_s,total_s,verify_s,total_clean_bytes,"
                << "entries,entries_per_s,bytes_per_entry,pprf_instances,default_base_ot_calls,ok,notes\n";
        }
        out << rmvoleNetBenchCsvEscape(row.method) << ','
            << rmvoleNetBenchCsvEscape(row.mode) << ','
            << rmvoleNetBenchCsvEscape(row.setupMode) << ','
            << rmvoleNetBenchCsvEscape(row.role) << ','
            << rmvoleNetBenchCsvEscape(row.network) << ','
            << rmvoleNetBenchCsvEscape(row.address) << ','
            << row.logN << ','
            << row.N << ','
            << row.t << ','
            << row.m << ','
            << row.blockSize << ','
            << row.reps << ','
            << std::setprecision(12) << row.splitInputSeconds << ','
            << row.splitInputBytes << ','
            << row.pprfSetupSeconds << ','
            << row.pprfBytes << ','
            << row.expandP0Seconds << ','
            << row.expandP1Seconds << ','
            << row.totalSeconds << ','
            << row.verifySeconds << ','
            << row.cleanProtocolBytes << ','
            << row.entries << ','
            << row.entriesPerSecond << ','
            << row.bytesPerEntry << ','
            << row.pprfInstances << ','
            << row.defaultBaseOtCalls << ','
            << (row.ok ? 1 : 0) << ','
            << rmvoleNetBenchCsvEscape(row.notes) << '\n';
    }

    inline void rmvoleNetBenchWriteNotes()
    {
        std::filesystem::create_directories("docs");
        std::ofstream out("docs/rmvole_net_experiment_notes.md");
        out << "# RM-VOLE Network Setup Plus Expand Benchmark\n\n"
            << "Phase 6F adds `--RMVOLE_NET_BENCH`, a semi-honest harness that runs real vector-valued libOTe `RegularPprf` setup over coproto sockets and then performs local deterministic RM-VOLE expansion on each role.\n\n"
            << "Phase 6K adds an optional `split-input` setup mode. This mode flips the PPRF roles so that P0 is the `RegularPprfReceiver` with sparse support choices and P1 is the `RegularPprfSender` programming masked payload shares. The current Phase 6K split-input product layer is deliberately marked `split-input-simulated`: a harness helper samples additive product shares for `Delta*s_i` and `Delta*e_i` and sends P1's programmed `-B_i` shares. This verifies the wiring and accounting shape, but it is not yet a secure OT/VOLE product-sharing protocol.\n\n"
            << "Phase 6L adds `split-input-vole`, which replaces the simulated product-share helper with real libOTe noisy VOLE over length `2t`. It instantiates `NoisyVoleReceiver<F,G,Ctx>` and `NoisyVoleSender<F,G,Ctx>` with `F = std::array<u64,M>`, `G = u64`, and `Ctx = CoeffCtxPrimeArray64<M>`. P0 owns the sparse coefficients `c=q_i` and receives `a`; P1 owns `Delta` and receives `b`; libOTe guarantees `a = b + c*Delta`. P0 uses `A=a`, P1 programs `-b`, and vector `RegularPprf` places these product shares at the sparse leaves.\n\n"
            << "## Relation\n\n"
            << "The benchmark checks the coordinate representation of `Z0 + Z1 = Delta * x`, where `x` is in `R_p`, and `Delta`, `Z0`, and `Z1` are represented as `m` base-field coordinate rows. Verification uses the existing `moduleMvoleVerify()` relation.\n\n"
            << "## Protocol Shape\n\n"
            << "- Setup uses two vector-valued `RegularPprf` executions: one for `Delta*s`, one for `Delta*e`.\n"
            << "- The regular PPRF domain is `domainSize = blockSize = N/t`, with `pointCount = t`.\n"
            << "- In `centralized` mode, P0 is the PPRF sender and interprets sender output as `v0 = -senderOut` and `u0 = -senderOut`; P1 is the PPRF receiver and uses `receiverOut`.\n"
            << "- In `split-input-simulated` mode, P0 is the PPRF receiver, adds its simulated product share `A_i` at selected leaves, and P1 is the PPRF sender storing `-senderOut`.\n"
            << "- In `split-input-vole` mode, P0 first runs libOTe noisy VOLE as receiver with `c=(s_0,...,s_{t-1},e_0,...,e_{t-1})`, P1 runs as sender with `Delta`, and the resulting `a,-b` shares are fed into the same vector PPRF placement path.\n"
            << "- P0 expands locally as `x = WHT(rho*s + e)` and `Z0 = WHT(rho*v0) + WHT(u0)`.\n"
            << "- P1 expands locally as `Z1 = WHT(rho*v1) + WHT(u1)`.\n\n"
            << "In `centralized` mode, the harness computes the full `Delta*s_i` and `Delta*e_i` payloads and gives them directly to `RegularPprfSender`. In `split-input` mode, the harness simulates shares `A_i - B_i = Delta*q_i`; P1 programs `-B_i`, P0 receives the punctured output and adds `A_i` at the selected leaf. In `split-input-vole` mode, the same share shape is generated by noisy VOLE instead of the simulated helper. In all three modes, the reconstructed sparse payload is still `Delta*q_i`.\n\n"
            << "## Harness Caveat\n\n"
            << "`centralized` mode is not secure input generation. The server samples the test instance and sends the client the common rho seed, Delta coordinates, and sparse offsets needed for choices/local expansion. These bytes are reported as `harness_metadata_bytes` and excluded from `clean_protocol_bytes` in bench mode. Verify mode may also send P0 openings after timing; those bytes are reported as `verification_opening_bytes`.\n\n"
            << "`split-input-simulated` mode removes full-product PPRF programming and does not send sparse offsets to P1, but it still centrally creates product shares. The `split_input_bytes` column counts the simulated payload bytes for transferring P1's programmed product shares; in TCP mode it is payload-level accounting and does not try to split coproto framing bytes at the send boundary. It is not the cost of a secure split-input primitive. A complete semi-honest implementation still needs scalar/subfield VOLE or OT-based multiplication sharing for the `2t` sparse coefficients.\n\n"
            << "`split-input-vole` mode uses real libOTe noisy VOLE for the product shares. Its `split_input_bytes` are measured from socket byte snapshots around the noisy VOLE call and include noisy VOLE's base OT and payload traffic. The harness still centrally samples the test instance on the server and sends `rhoSeed` and `Delta` to the client as metadata so the two-process benchmark can be orchestrated; sparse offsets and values are not sent to P1 in this mode.\n\n"
            << "## Commands\n\n"
            << "```bash\n"
            << "./build/main --RMVOLE_NET_BENCH local 12 8 8 verify\n"
            << "./build/main --RMVOLE_NET_BENCH local 12 8 8 bench\n"
            << "./build/main --RMVOLE_NET_BENCH local 12 8 8 verify split-input\n"
            << "./build/main --RMVOLE_NET_BENCH local 12 8 8 bench split-input\n"
            << "./build/main --RMVOLE_NET_BENCH local 12 8 8 verify split-input-vole\n"
            << "./build/main --RMVOLE_NET_BENCH server 0.0.0.0 12300 12 8 8 3 bench centralized\n"
            << "./build/main --RMVOLE_NET_BENCH client 127.0.0.1 12300 12 8 8 3 bench centralized\n"
            << "./build/main --RMVOLE_NET_BENCH server 0.0.0.0 12350 12 8 8 3 bench split-input\n"
            << "./build/main --RMVOLE_NET_BENCH client 127.0.0.1 12350 12 8 8 3 bench split-input\n"
            << "./build/main --RMVOLE_NET_BENCH server 0.0.0.0 12360 12 8 8 3 bench split-input-vole\n"
            << "./build/main --RMVOLE_NET_BENCH client 127.0.0.1 12360 12 8 8 3 bench split-input-vole\n"
            << "```\n\n"
            << "Results are appended to `docs/rmvole_net_benchmark.csv`. The detailed split/PPRF timing and byte breakdown is also appended to `docs/rmvole_split_input_benchmark.csv`. Real noisy-VOLE split-input rows are additionally written to `docs/rmvole_split_input_vole_benchmark.csv`.\n\n"
            << "## Limitations\n\n"
            << "- Semi-honest correctness/API harness only; no malicious checks.\n"
            << "- `split-input-vole` is semi-honest noisy VOLE, not silent VOLE and not malicious-secure.\n"
            << "- Supported vector dimensions are fixed compile-time `m = 8, 16, 32, 64`.\n"
            << "- This reports networked setup plus local expansion, not a fully integrated production protocol.\n";
    }

    inline void rmvoleNetBenchPrintRow(const RmvoleNetBenchRow& row)
    {
        std::cout << std::fixed << std::setprecision(6)
                  << "RMVOLE_NET_BENCH"
                  << " mode=" << row.mode
                  << " setup_mode=" << row.setupMode
                  << " role=" << row.role
                  << " network=" << row.network
                  << " address=" << row.address
                  << " logN=" << row.logN
                  << " N=" << row.N
                  << " t=" << row.t
                  << " m=" << row.m
                  << " blockSize=" << row.blockSize
                  << " reps=" << row.reps
                  << " split_input_s=" << row.splitInputSeconds
                  << " pprf_setup_s=" << row.pprfSetupSeconds
                  << " setup_s=" << row.setupSeconds
                  << " expand_p0_s=" << row.expandP0Seconds
                  << " expand_p1_s=" << row.expandP1Seconds
                  << " total_s=" << row.totalSeconds
                  << " verify_s=" << row.verifySeconds
                  << " bytes_sent_by_role=" << row.bytesSentByRole
                  << " bytes_received_by_role=" << row.bytesReceivedByRole
                  << " total_socket_bytes=" << row.totalSocketBytes
                  << " harness_metadata_bytes=" << row.harnessMetadataBytes
                  << " verification_opening_bytes=" << row.verificationOpeningBytes
                  << " split_input_bytes=" << row.splitInputBytes
                  << " pprf_bytes=" << row.pprfBytes
                  << " clean_protocol_bytes=" << row.cleanProtocolBytes
                  << " entries=" << row.entries
                  << " entries_per_s=" << row.entriesPerSecond
                  << " bytes_per_entry=" << row.bytesPerEntry
                  << " pprf_instances=" << row.pprfInstances
                  << " default_base_ot_calls=" << row.defaultBaseOtCalls
                  << " notes=" << row.notes
                  << " " << (row.ok ? "PASS" : "FAIL")
                  << std::endl;
    }

    template<u64 M>
    RmvoleNetBenchRow rmvoleNetBenchRunLocalDispatched(
        const RmvoleNetBenchParams& params,
        RmvoleNetBenchMode mode,
        RmvoleNetBenchSetupMode setupMode)
    {
        return rmvoleNetBenchRunLocalTyped<M>(params, mode, setupMode);
    }

    inline RmvoleNetBenchRow rmvoleNetBenchRunLocal(
        const RmvoleNetBenchParams& params,
        RmvoleNetBenchMode mode,
        RmvoleNetBenchSetupMode setupMode)
    {
        switch (params.m)
        {
        case 8:
            return rmvoleNetBenchRunLocalDispatched<8>(params, mode, setupMode);
        case 16:
            return rmvoleNetBenchRunLocalDispatched<16>(params, mode, setupMode);
        case 32:
            return rmvoleNetBenchRunLocalDispatched<32>(params, mode, setupMode);
        case 64:
            return rmvoleNetBenchRunLocalDispatched<64>(params, mode, setupMode);
        default:
            throw std::runtime_error("RMVOLE_NET_BENCH supports m = 8, 16, 32, 64.");
        }
    }

    inline RmvoleNetBenchRow rmvoleNetBenchRunTcpRole(
        bool server,
        const std::string& host,
        const std::string& port,
        const RmvoleNetBenchParams& params,
        u64 reps,
        RmvoleNetBenchMode mode,
        RmvoleNetBenchSetupMode setupMode)
    {
        switch (params.m)
        {
        case 8:
            return rmvoleNetBenchRunTcpRoleTyped<8>(server, host, port, params, reps, mode, setupMode);
        case 16:
            return rmvoleNetBenchRunTcpRoleTyped<16>(server, host, port, params, reps, mode, setupMode);
        case 32:
            return rmvoleNetBenchRunTcpRoleTyped<32>(server, host, port, params, reps, mode, setupMode);
        case 64:
            return rmvoleNetBenchRunTcpRoleTyped<64>(server, host, port, params, reps, mode, setupMode);
        default:
            throw std::runtime_error("RMVOLE_NET_BENCH supports m = 8, 16, 32, 64.");
        }
    }

    inline int RmvoleNetBench(int argc, char** argv)
    {
        rmvoleNetBenchWriteNotes();

        if (argc >= 3 && std::string(argv[2]) == "local")
        {
            if (argc != 7 && argc != 8)
            {
                std::cerr << "Usage: ./build/main --RMVOLE_NET_BENCH local <logN> <t> <m> <verify|bench> [centralized|split-input|split-input-vole]" << std::endl;
                return 1;
            }

            auto params = rmvoleNetBenchMakeParams(
                static_cast<u64>(std::stoull(argv[3])),
                static_cast<u64>(std::stoull(argv[4])),
                static_cast<u64>(std::stoull(argv[5])));
            auto mode = rmvoleNetBenchParseMode(argv[6]);
            auto setupMode = argc == 8 ? rmvoleNetBenchParseSetupMode(argv[7]) : RmvoleNetBenchSetupMode::Centralized;
            auto row = rmvoleNetBenchRunLocal(params, mode, setupMode);
            rmvoleNetBenchPrintRow(row);
            rmvoleNetBenchWriteCsv(row);
            rmvoleNetBenchWriteSplitInputCsv(row);
            rmvoleNetBenchWriteSplitInputVoleCsv(row);
            return row.ok ? 0 : 1;
        }

        if (argc >= 3 && (std::string(argv[2]) == "server" || std::string(argv[2]) == "client"))
        {
            if (argc != 10 && argc != 11)
            {
                std::cerr << "Usage: ./build/main --RMVOLE_NET_BENCH server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps> <verify|bench> [centralized|split-input|split-input-vole]\n"
                          << "       ./build/main --RMVOLE_NET_BENCH client <host> <port> <logN> <t> <m> <reps> <verify|bench> [centralized|split-input|split-input-vole]" << std::endl;
                return 1;
            }

            auto server = std::string(argv[2]) == "server";
            auto host = std::string(argv[3]);
            auto port = std::string(argv[4]);
            auto params = rmvoleNetBenchMakeParams(
                static_cast<u64>(std::stoull(argv[5])),
                static_cast<u64>(std::stoull(argv[6])),
                static_cast<u64>(std::stoull(argv[7])));
            auto reps = static_cast<u64>(std::stoull(argv[8]));
            auto mode = rmvoleNetBenchParseMode(argv[9]);
            auto setupMode = argc == 11 ? rmvoleNetBenchParseSetupMode(argv[10]) : RmvoleNetBenchSetupMode::Centralized;
            if (!reps)
                throw std::runtime_error("RMVOLE_NET_BENCH TCP mode requires reps > 0.");

            auto row = rmvoleNetBenchRunTcpRole(server, host, port, params, reps, mode, setupMode);
            rmvoleNetBenchPrintRow(row);
            rmvoleNetBenchWriteCsv(row);
            rmvoleNetBenchWriteSplitInputCsv(row);
            rmvoleNetBenchWriteSplitInputVoleCsv(row);
            return row.ok ? 0 : 1;
        }

        std::cerr << "Usage: ./build/main --RMVOLE_NET_BENCH local <logN> <t> <m> <verify|bench> [centralized|split-input|split-input-vole]\n"
                  << "       ./build/main --RMVOLE_NET_BENCH server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps> <verify|bench> [centralized|split-input|split-input-vole]\n"
                  << "       ./build/main --RMVOLE_NET_BENCH client <host> <port> <logN> <t> <m> <reps> <verify|bench> [centralized|split-input|split-input-vole]" << std::endl;
        return 1;
    }
}
