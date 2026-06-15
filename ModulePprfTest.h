#pragma once

#include "Coeff128.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "libOTe/Tools/Pprf/RegularPprf.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <string>
#include <vector>

namespace osuCrypto
{
    struct ModulePprfTestParams
    {
        u64 N = 1024;
        u64 t = 8;
        u64 m = 8;
    };

    struct ModulePprfTiming
    {
        double setupSeconds = 0;
        double expandSeconds = 0;
        bool ok = false;
    };

    template<typename F, typename Ctx>
    ModulePprfTiming modulePprfRunScalar(
        const ModulePprfTestParams& params,
        u64 coord,
        bool verboseFail = false,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;

        ModulePprfTiming timing;
        PRNG prng(sysRandomSeed());
        RegularPprfSender<F, F, Ctx> sender;
        RegularPprfReceiver<F, F, Ctx> receiver;
        auto sockets = cp::LocalAsyncSocket::makePair();
        auto domain = params.N / params.t;
        auto pointCount = params.t;

        VecF delta, senderOut, receiverOut;
        ctx.resize(delta, pointCount);
        ctx.resize(senderOut, params.N);
        ctx.resize(receiverOut, params.N);

        auto start = omp_get_wtime();
        sender.configure(domain, pointCount);
        receiver.configure(domain, pointCount);

        auto numBaseOts = sender.baseOtCount();
        std::vector<std::array<block, 2>> senderBaseOts(numBaseOts);
        std::vector<block> receiverBaseOts(numBaseOts);
        auto receiverChoiceBits = receiver.sampleChoiceBits(prng);

        prng.get(senderBaseOts.data(), senderBaseOts.size());
        for (u64 i = 0; i < numBaseOts; ++i)
            receiverBaseOts[i] = senderBaseOts[i][receiverChoiceBits[i]];

        sender.setBase(senderBaseOts);
        receiver.setBase(receiverBaseOts);

        for (u64 i = 0; i < pointCount; ++i)
            ctx.fromBlock(delta[i], prng.get<block>() ^ block(coord, i));

        std::vector<u64> points(pointCount);
        receiver.getPoints(points, PprfOutputFormat::ByTreeIndex);
        auto end = omp_get_wtime();
        timing.setupSeconds = end - start;

        start = omp_get_wtime();
        auto s = sender.expand(
            sockets[0],
            delta,
            prng.get<block>() ^ block(coord, 0xA5A5),
            senderOut,
            PprfOutputFormat::ByTreeIndex,
            true,
            1,
            ctx);
        auto r = receiver.expand(
            sockets[1],
            receiverOut,
            PprfOutputFormat::ByTreeIndex,
            true,
            1,
            ctx);
        macoro::sync_wait(macoro::when_all_ready(std::move(s), std::move(r)));
        end = omp_get_wtime();
        timing.expandSeconds = end - start;

        timing.ok = true;
        for (u64 tree = 0; tree < pointCount; ++tree)
        {
            for (u64 leaf = 0; leaf < domain; ++leaf)
            {
                auto idx = tree * domain + leaf;
                F expected;
                if (points[tree] == leaf)
                    ctx.plus(expected, senderOut[idx], delta[tree]);
                else
                    ctx.copy(expected, senderOut[idx]);

                if (!ctx.eq(expected, receiverOut[idx]))
                {
                    timing.ok = false;
                    if (verboseFail)
                    {
                        std::cout << "MODULE_PPRF_TEST FAIL coord=" << coord
                                  << " tree=" << tree
                                  << " leaf=" << leaf
                                  << " point=" << points[tree]
                                  << " expected=" << ctx.str(expected)
                                  << " actual=" << ctx.str(receiverOut[idx])
                                  << std::endl;
                    }
                    return timing;
                }
            }
        }

        return timing;
    }

    template<typename F, typename Ctx>
    bool modulePprfRunAdapter(const ModulePprfTestParams& params, const std::string& label)
    {
        auto scalar = modulePprfRunScalar<F, Ctx>(params, 0, true);

        ModulePprfTiming vectorTiming;
        vectorTiming.ok = scalar.ok;
        for (u64 h = 0; h < params.m; ++h)
        {
            auto one = modulePprfRunScalar<F, Ctx>(params, h, true);
            vectorTiming.setupSeconds += one.setupSeconds;
            vectorTiming.expandSeconds += one.expandSeconds;
            vectorTiming.ok = vectorTiming.ok && one.ok;
        }

        auto outputElems = params.N * params.m;
        auto vectorTotal = vectorTiming.setupSeconds + vectorTiming.expandSeconds;
        auto scalarTotal = scalar.setupSeconds + scalar.expandSeconds;
        auto throughput = vectorTotal > 0 ? static_cast<double>(outputElems) / vectorTotal : 0.0;

        std::cout << std::fixed << std::setprecision(6)
                  << "MODULE_PPRF_TEST " << label
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " domain=" << (params.N / params.t)
                  << " output_elems=" << outputElems
                  << " scalar_setup_s=" << scalar.setupSeconds
                  << " scalar_expand_s=" << scalar.expandSeconds
                  << " scalar_total_s=" << scalarTotal
                  << " adapter_setup_s=" << vectorTiming.setupSeconds
                  << " adapter_expand_s=" << vectorTiming.expandSeconds
                  << " adapter_total_s=" << vectorTotal
                  << " adapter_throughput_elems_per_s=" << throughput
                  << " " << (vectorTiming.ok ? "PASS" : "FAIL")
                  << std::endl;

        return vectorTiming.ok;
    }

    inline ModulePprfTestParams modulePprfTestPreset(u64 preset)
    {
        ModulePprfTestParams params;
        if (preset == 1)
        {
            params.N = 1024;
            params.t = 8;
            params.m = 8;
        }
        else
        {
            params.N = 256;
            params.t = 4;
            params.m = 4;
        }
        return params;
    }

    inline int ModulePprf_Test(u64 preset)
    {
        auto params = modulePprfTestPreset(preset);
        if (params.N % params.t)
            throw std::runtime_error("MODULE_PPRF_TEST requires t to divide N.");

        auto ok64 = modulePprfRunAdapter<u64, CoeffCtxIntegerPrime_64>(params, "Fp64");
        return ok64 ? 0 : 1;
    }
}
