#pragma once

#include "ModuleMVOLE.h"
#include "ModuleMvolePprfIntegration.h"
#include "RmvolePprfNetSetupTest.h"
#include "coproto/Socket/AsioSocket.h"
#include "coproto/Socket/LocalAsyncSock.h"
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
        std::string role = "local";
        std::string network = "local-async-pair";
        std::string address;
        u64 logN = 0;
        u64 N = 0;
        u64 t = 0;
        u64 m = 0;
        u64 blockSize = 0;
        u64 reps = 1;
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
    RmvoleNetBenchRow rmvoleNetBenchRunLocalTyped(
        const RmvoleNetBenchParams& params,
        RmvoleNetBenchMode mode)
    {
        RmvoleNetBenchRow row;
        row.mode = rmvoleNetBenchModeName(mode);
        row.logN = params.logN;
        row.N = params.N;
        row.t = params.t;
        row.m = params.m;
        row.blockSize = params.blockSize;
        row.entries = params.m * params.N;
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
        rmvoleNetBenchLocalVectorPprf<M>(params, instance, p0Gen.rowMasks, p1Gen.rowMasks, true, sockets[0], sockets[1], row.setupSeconds, prng, vectorCtx, scalarCtx);
        rmvoleNetBenchLocalVectorPprf<M>(params, instance, p0Gen.rowMasks, p1Gen.rowMasks, false, sockets[0], sockets[1], row.setupSeconds, prng, vectorCtx, scalarCtx);

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
        row.cleanProtocolBytes = row.totalSocketBytes;
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
        RmvoleNetBenchMode mode)
    {
        auto address = host + ":" + port;
        auto socket = cp::asioConnect(address, server);
        PRNG prng(sysRandomSeed());
        CoeffCtxIntegerPrime_64 scalarCtx;
        CoeffCtxPrimeArray64<M> vectorCtx;
        std::vector<double> setupTimes, expandP0Times, expandP1Times, totalTimes, verifyTimes;
        bool ok = true;

        RmvoleNetBenchRow row;
        row.mode = rmvoleNetBenchModeName(mode);
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
        row.notes = server
            ? "tcp_two_process; p0_sender; vector_fixed_prime_setup; local_expand; centralized_test_instance"
            : "tcp_two_process; p1_receiver; vector_fixed_prime_setup; local_expand; centralized_test_instance";

        for (u64 rep = 0; rep < reps; ++rep)
        {
            double setup = 0;
            double expandP0 = 0;
            double expandP1 = 0;
            double verify = 0;

            if (server)
            {
                auto instance = rmvoleNetBenchSampleInstance<M>(params, prng);
                rmvoleNetBenchSendInstanceMetadata<M>(socket, instance);
                row.harnessMetadataBytes += rmvoleNetBenchMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p0Gen;
                ModuleMVOLEP0Output<u64, CoeffCtxIntegerPrime_64> p0;
                rmvoleNetBenchBuildP0Gen<M>(p0Gen, params, instance, scalarCtx);
                rmvoleNetBenchTcpServerVectorPprf<M>(params, instance, p0Gen.rowMasks, true, socket, setup, prng, vectorCtx, scalarCtx);
                rmvoleNetBenchTcpServerVectorPprf<M>(params, instance, p0Gen.rowMasks, false, socket, setup, prng, vectorCtx, scalarCtx);

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
                auto instance = rmvoleNetBenchRecvInstanceMetadata<M>(socket, params);
                row.harnessMetadataBytes += rmvoleNetBenchMetadataBytes(params);

                ModuleMVOLEGenState<u64, CoeffCtxIntegerPrime_64> p1Gen;
                ModuleMVOLEP1Output<u64, CoeffCtxIntegerPrime_64> p1;
                rmvoleNetBenchBuildP1Gen<M>(p1Gen, params, instance, scalarCtx);
                rmvoleNetBenchTcpClientVectorPprf<M>(params, instance, p1Gen.rowMasks, true, socket, setup, prng, vectorCtx);
                rmvoleNetBenchTcpClientVectorPprf<M>(params, instance, p1Gen.rowMasks, false, socket, setup, prng, vectorCtx);

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

            setupTimes.push_back(setup);
            expandP0Times.push_back(expandP0);
            expandP1Times.push_back(expandP1);
            totalTimes.push_back(setup + std::max(expandP0, expandP1));
            verifyTimes.push_back(verify);
            if (!ok)
                break;
        }

        macoro::sync_wait(socket.flush());
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

    inline void rmvoleNetBenchWriteNotes()
    {
        std::filesystem::create_directories("docs");
        std::ofstream out("docs/rmvole_net_experiment_notes.md");
        out << "# RM-VOLE Network Setup Plus Expand Benchmark\n\n"
            << "Phase 6F adds `--RMVOLE_NET_BENCH`, a semi-honest harness that runs real vector-valued libOTe `RegularPprf` setup over coproto sockets and then performs local deterministic RM-VOLE expansion on each role.\n\n"
            << "## Relation\n\n"
            << "The benchmark checks the coordinate representation of `Z0 + Z1 = Delta * x`, where `x` is in `R_p`, and `Delta`, `Z0`, and `Z1` are represented as `m` base-field coordinate rows. Verification uses the existing `moduleMvoleVerify()` relation.\n\n"
            << "## Protocol Shape\n\n"
            << "- Setup uses two vector-valued `RegularPprf` executions: one for `Delta*s`, one for `Delta*e`.\n"
            << "- The regular PPRF domain is `domainSize = blockSize = N/t`, with `pointCount = t`.\n"
            << "- P0 interprets sender output as `v0 = -senderOut` and `u0 = -senderOut` for the two sparse vectors.\n"
            << "- P1 interprets receiver output as `v1 = receiverOut` and `u1 = receiverOut`.\n"
            << "- P0 expands locally as `x = WHT(rho*s + e)` and `Z0 = WHT(rho*v0) + WHT(u0)`.\n"
            << "- P1 expands locally as `Z1 = WHT(rho*v1) + WHT(u1)`.\n\n"
            << "## Harness Caveat\n\n"
            << "This is not secure input generation. The server samples the test instance and sends the client the common rho seed, Delta coordinates, and sparse offsets needed for choices/local expansion. These bytes are reported as `harness_metadata_bytes` and excluded from `clean_protocol_bytes` in bench mode. Verify mode may also send P0 openings after timing; those bytes are reported as `verification_opening_bytes`.\n\n"
            << "## Commands\n\n"
            << "```bash\n"
            << "./build/main --RMVOLE_NET_BENCH local 12 8 8 verify\n"
            << "./build/main --RMVOLE_NET_BENCH local 12 8 8 bench\n"
            << "./build/main --RMVOLE_NET_BENCH server 0.0.0.0 12300 12 8 8 3 bench\n"
            << "./build/main --RMVOLE_NET_BENCH client 127.0.0.1 12300 12 8 8 3 bench\n"
            << "```\n\n"
            << "Results are appended to `docs/rmvole_net_benchmark.csv`.\n\n"
            << "## Limitations\n\n"
            << "- Semi-honest correctness/API harness only; no malicious checks.\n"
            << "- Centralized test input generation remains a harness artifact.\n"
            << "- Supported vector dimensions are fixed compile-time `m = 8, 16, 32, 64`.\n"
            << "- This reports networked setup plus local expansion, not a fully integrated production protocol.\n";
    }

    inline void rmvoleNetBenchPrintRow(const RmvoleNetBenchRow& row)
    {
        std::cout << std::fixed << std::setprecision(6)
                  << "RMVOLE_NET_BENCH"
                  << " mode=" << row.mode
                  << " role=" << row.role
                  << " network=" << row.network
                  << " address=" << row.address
                  << " logN=" << row.logN
                  << " N=" << row.N
                  << " t=" << row.t
                  << " m=" << row.m
                  << " blockSize=" << row.blockSize
                  << " reps=" << row.reps
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
    RmvoleNetBenchRow rmvoleNetBenchRunLocalDispatched(const RmvoleNetBenchParams& params, RmvoleNetBenchMode mode)
    {
        return rmvoleNetBenchRunLocalTyped<M>(params, mode);
    }

    inline RmvoleNetBenchRow rmvoleNetBenchRunLocal(const RmvoleNetBenchParams& params, RmvoleNetBenchMode mode)
    {
        switch (params.m)
        {
        case 8:
            return rmvoleNetBenchRunLocalDispatched<8>(params, mode);
        case 16:
            return rmvoleNetBenchRunLocalDispatched<16>(params, mode);
        case 32:
            return rmvoleNetBenchRunLocalDispatched<32>(params, mode);
        case 64:
            return rmvoleNetBenchRunLocalDispatched<64>(params, mode);
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
        RmvoleNetBenchMode mode)
    {
        switch (params.m)
        {
        case 8:
            return rmvoleNetBenchRunTcpRoleTyped<8>(server, host, port, params, reps, mode);
        case 16:
            return rmvoleNetBenchRunTcpRoleTyped<16>(server, host, port, params, reps, mode);
        case 32:
            return rmvoleNetBenchRunTcpRoleTyped<32>(server, host, port, params, reps, mode);
        case 64:
            return rmvoleNetBenchRunTcpRoleTyped<64>(server, host, port, params, reps, mode);
        default:
            throw std::runtime_error("RMVOLE_NET_BENCH supports m = 8, 16, 32, 64.");
        }
    }

    inline int RmvoleNetBench(int argc, char** argv)
    {
        rmvoleNetBenchWriteNotes();

        if (argc >= 3 && std::string(argv[2]) == "local")
        {
            if (argc != 7)
            {
                std::cerr << "Usage: ./build/main --RMVOLE_NET_BENCH local <logN> <t> <m> <verify|bench>" << std::endl;
                return 1;
            }

            auto params = rmvoleNetBenchMakeParams(
                static_cast<u64>(std::stoull(argv[3])),
                static_cast<u64>(std::stoull(argv[4])),
                static_cast<u64>(std::stoull(argv[5])));
            auto mode = rmvoleNetBenchParseMode(argv[6]);
            auto row = rmvoleNetBenchRunLocal(params, mode);
            rmvoleNetBenchPrintRow(row);
            rmvoleNetBenchWriteCsv(row);
            return row.ok ? 0 : 1;
        }

        if (argc >= 3 && (std::string(argv[2]) == "server" || std::string(argv[2]) == "client"))
        {
            if (argc != 10)
            {
                std::cerr << "Usage: ./build/main --RMVOLE_NET_BENCH server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps> <verify|bench>\n"
                          << "       ./build/main --RMVOLE_NET_BENCH client <host> <port> <logN> <t> <m> <reps> <verify|bench>" << std::endl;
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
            if (!reps)
                throw std::runtime_error("RMVOLE_NET_BENCH TCP mode requires reps > 0.");

            auto row = rmvoleNetBenchRunTcpRole(server, host, port, params, reps, mode);
            rmvoleNetBenchPrintRow(row);
            rmvoleNetBenchWriteCsv(row);
            return row.ok ? 0 : 1;
        }

        std::cerr << "Usage: ./build/main --RMVOLE_NET_BENCH local <logN> <t> <m> <verify|bench>\n"
                  << "       ./build/main --RMVOLE_NET_BENCH server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps> <verify|bench>\n"
                  << "       ./build/main --RMVOLE_NET_BENCH client <host> <port> <logN> <t> <m> <reps> <verify|bench>" << std::endl;
        return 1;
    }
}
