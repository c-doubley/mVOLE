#pragma once

#include "ModuleMVOLE.h"
#include "ModuleVectorPprfTest.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace osuCrypto
{
    struct ModuleMVOLEPprfTiming
    {
        double pprfSSeconds = 0;
        double pprfESeconds = 0;
        double expandP0Seconds = 0;
        double expandP1Seconds = 0;
        double verifySeconds = 0;
        double totalSeconds = 0;
        u64 totalBlocks = 0;
        u64 blockSize = 0;
        u64 expandedLeavesPerSparseVector = 0;
        u64 expectedLeavesPerSparseVector = 0;
        double expandedLeavesOverN = 0;
        bool ok = false;
    };

    inline u64 moduleMvoleRegularBlockSize(const ModuleMVOLEParams& params)
    {
        if (params.t == 0 || params.N % params.t)
            throw std::runtime_error("MODULE_MVOLE_PPRF regular mode requires t to divide N.");

        auto blockSize = params.N / params.t;
        if (!moduleVectorPprfIsPowerOfTwo(blockSize))
            throw std::runtime_error("MODULE_MVOLE_PPRF regular mode requires N/t to be a power of two.");

        return blockSize;
    }

    template<typename F, typename Ctx>
    void moduleMvoleSampleRegularSparse(
        typename Ctx::template Vec<F>& out,
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx& ctx)
    {
        auto blockSize = moduleMvoleRegularBlockSize(params);
        ctx.resize(out, params.N);
        ctx.zero(out.begin(), out.end());

        for (u64 i = 0; i < params.t; ++i)
        {
            auto idx = i * blockSize + (prng.get<u64>() % blockSize);
            F zero{};
            do
            {
                ctx.fromBlock(out[idx], prng.get());
            } while (ctx.eq(out[idx], zero));
        }
    }

    template<typename F, typename Ctx>
    ModuleMVOLEBaseState<F, Ctx> moduleMvoleGenBaseRegular(
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;

        ModuleMVOLEBaseState<F, Ctx> state;
        VecF rhoS;

        ctx.resize(state.rho, params.N);
        for (u64 j = 0; j < params.N; ++j)
            ctx.fromBlock(state.rho[j], prng.get());
        ctx.resize(state.rhoWht, params.N);
        ctx.copy(state.rho.begin(), state.rho.end(), state.rhoWht.begin());
        wht<F, Ctx>(state.rhoWht, static_cast<int>(params.N));

        moduleMvoleSampleRegularSparse<F, Ctx>(state.s, params, prng, ctx);
        moduleMvoleSampleRegularSparse<F, Ctx>(state.e, params, prng, ctx);

        moduleMvoleRingMulSparseRhs<F, Ctx>(state.rho, state.s, rhoS, ctx);
        ctx.resize(state.b, params.N);
        for (u64 j = 0; j < params.N; ++j)
            ctx.plus(state.b[j], rhoS[j], state.e[j]);

        return state;
    }

    template<typename F, typename Ctx>
    void moduleMvoleBuildRegularVectorPprfPoints(
        const ModuleMVOLEParams& params,
        const typename Ctx::template Vec<F>& sparse,
        const typename Ctx::template Vec<F>& Delta,
        std::vector<ModuleVectorPprfPoint<F, Ctx>>& points,
        PRNG& prng,
        Ctx& ctx)
    {
        auto blockSize = moduleMvoleRegularBlockSize(params);
        F zero{};
        points.clear();
        points.reserve(params.t);

        for (u64 i = 0; i < params.t; ++i)
        {
            auto blockStart = i * blockSize;
            u64 offset = blockSize;
            for (u64 j = 0; j < blockSize; ++j)
            {
                if (!ctx.eq(sparse[blockStart + j], zero))
                {
                    offset = j;
                    break;
                }
            }

            if (offset == blockSize)
                throw std::runtime_error("MODULE_MVOLE_PPRF regular sparse block has no nonzero coefficient.");

            auto& point = points.emplace_back();
            point.alpha = offset;
            point.rootSeed = prng.get<block>();
            ctx.resize(point.beta, params.m);

            auto coeff = sparse[blockStart + offset];
            for (u64 h = 0; h < params.m; ++h)
                ctx.mul(point.beta[h], Delta[h], coeff);
        }
    }

    template<typename F, typename Ctx>
    void moduleMvoleInitPprfRows(
        const ModuleMVOLEParams& params,
        std::vector<ModuleMVOLERowMask<F, Ctx>>& rowMasks,
        Ctx& ctx)
    {
        rowMasks.resize(params.m);

        for (u64 h = 0; h < params.m; ++h)
        {
            auto& row = rowMasks[h];
            ctx.resize(row.V, params.N);
            ctx.resize(row.W0, params.N);
            ctx.resize(row.U, params.N);
            ctx.resize(row.W1, params.N);
            ctx.zero(row.V.begin(), row.V.end());
            ctx.zero(row.W0.begin(), row.W0.end());
            ctx.zero(row.U.begin(), row.U.end());
            ctx.zero(row.W1.begin(), row.W1.end());
        }
    }

    template<typename F, typename Ctx>
    void moduleMvoleAddToVec(
        typename Ctx::template Vec<F>& vec,
        u64 idx,
        const F& value,
        Ctx& ctx)
    {
        F sum;
        ctx.plus(sum, vec[idx], value);
        vec[idx] = sum;
    }

    template<typename F, typename Ctx>
    u64 moduleMvoleExpandRegularVectorPprfIntoRows(
        const ModuleMVOLEParams& params,
        const std::vector<ModuleVectorPprfPoint<F, Ctx>>& points,
        std::vector<ModuleMVOLERowMask<F, Ctx>>& rowMasks,
        bool useSRows,
        Ctx& ctx)
    {
        auto blockSize = moduleMvoleRegularBlockSize(params);
        AES aes(block(0x4d565052464c4541ull, 0x4656414c55455331ull));
        std::vector<block> leaves;
        leaves.reserve(blockSize);
        u64 expandedLeaves = 0;

        // Regular sparse prototype: one point function per block. Each PPRF
        // tree expands only over the block domain, then writes to the global
        // ring coordinate blockStart + j.
        for (u64 i = 0; i < static_cast<u64>(points.size()); ++i)
        {
            const auto& point = points[i];
            auto blockStart = i * blockSize;
            moduleVectorPprfExpandSharedTree(point.rootSeed, blockSize, leaves);
            expandedLeaves += blockSize;

            for (u64 j = 0; j < blockSize; ++j)
            {
                auto globalIdx = blockStart + j;
                for (u64 h = 0; h < params.m; ++h)
                {
                    F value;
                    ctx.fromBlock(value, aes.hashBlock(leaves[j] ^ block(i ^ h, h)));

                    auto& share0 = useSRows ? rowMasks[h].V : rowMasks[h].U;
                    auto& share1 = useSRows ? rowMasks[h].W0 : rowMasks[h].W1;
                    moduleMvoleAddToVec<F, Ctx>(share0, globalIdx, value, ctx);
                    moduleMvoleAddToVec<F, Ctx>(share1, globalIdx, moduleVectorPprfNeg<F, Ctx>(value, ctx), ctx);
                }
            }

            auto globalAlpha = blockStart + point.alpha;
            for (u64 h = 0; h < params.m; ++h)
            {
                auto& share1 = useSRows ? rowMasks[h].W0 : rowMasks[h].W1;
                moduleMvoleAddToVec<F, Ctx>(share1, globalAlpha, point.beta[h], ctx);
            }
        }

        return expandedLeaves;
    }

    template<typename F, typename Ctx>
    void moduleMvoleGenPprfSetup(
        ModuleMVOLEGenState<F, Ctx>& gen,
        const ModuleMVOLEParams& params,
        ModuleMVOLEPprfTiming& timing,
        PRNG& prng,
        Ctx ctx = {})
    {
        std::vector<ModuleVectorPprfPoint<F, Ctx>> points;

        auto blockSize = moduleMvoleRegularBlockSize(params);
        timing.totalBlocks = params.t;
        timing.blockSize = blockSize;
        timing.expectedLeavesPerSparseVector = params.N;

        gen.base = moduleMvoleGenBaseRegular<F, Ctx>(params, prng, ctx);
        moduleMvoleGenDelta<F, Ctx>(gen.Delta, params, prng, ctx);
        moduleMvoleInitPprfRows<F, Ctx>(params, gen.rowMasks, ctx);

        auto start = omp_get_wtime();
        moduleMvoleBuildRegularVectorPprfPoints<F, Ctx>(params, gen.base.s, gen.Delta, points, prng, ctx);
        auto expandedS = moduleMvoleExpandRegularVectorPprfIntoRows<F, Ctx>(params, points, gen.rowMasks, true, ctx);
        auto end = omp_get_wtime();
        timing.pprfSSeconds = end - start;

        start = omp_get_wtime();
        moduleMvoleBuildRegularVectorPprfPoints<F, Ctx>(params, gen.base.e, gen.Delta, points, prng, ctx);
        auto expandedE = moduleMvoleExpandRegularVectorPprfIntoRows<F, Ctx>(params, points, gen.rowMasks, false, ctx);
        end = omp_get_wtime();
        timing.pprfESeconds = end - start;
        timing.expandedLeavesPerSparseVector = expandedS;
        if (expandedS != expandedE)
            throw std::runtime_error("MODULE_MVOLE_PPRF regular s/e expanded leaf counts differ.");
        timing.expandedLeavesOverN = params.N ? static_cast<double>(expandedS) / static_cast<double>(params.N) : 0;
    }

    template<typename F, typename Ctx>
    ModuleMVOLEPprfTiming moduleMvolePprfRunOnce(
        const ModuleMVOLEParams& params,
        Ctx ctx = {})
    {
        PRNG prng(sysRandomSeed());
        ModuleMVOLEGenState<F, Ctx> gen;
        ModuleMVOLEResult<F, Ctx> result;
        ModuleMVOLEPprfTiming timing;

        auto totalStart = omp_get_wtime();
        moduleMvoleGenPprfSetup<F, Ctx>(gen, params, timing, prng, ctx);

        auto start = omp_get_wtime();
        moduleMvoleExpandP0<F, Ctx>(result.p0, params, gen, ctx);
        auto end = omp_get_wtime();
        timing.expandP0Seconds = end - start;

        start = omp_get_wtime();
        moduleMvoleExpandP1<F, Ctx>(result.p1, params, gen, ctx);
        end = omp_get_wtime();
        timing.expandP1Seconds = end - start;

        start = omp_get_wtime();
        timing.ok = moduleMvoleVerify<F, Ctx>(result, params, ctx, &std::cout);
        end = omp_get_wtime();
        timing.verifySeconds = end - start;
        timing.totalSeconds = end - totalStart;

        return timing;
    }

    inline double moduleMvolePprfTotalSeconds(const ModuleMVOLEPprfTiming& timing)
    {
        if (timing.totalSeconds > 0)
            return timing.totalSeconds;

        return timing.pprfSSeconds
            + timing.pprfESeconds
            + timing.expandP0Seconds
            + timing.expandP1Seconds
            + timing.verifySeconds;
    }

    template<typename F, typename Ctx>
    bool runModuleMvolePprf(const ModuleMVOLEParams& params, const std::string& label)
    {
        auto timing = moduleMvolePprfRunOnce<F, Ctx>(params);
        auto total = moduleMvolePprfTotalSeconds(timing);
        auto outputElems = params.m * params.N;
        auto throughput = total > 0 ? static_cast<double>(outputElems) / total : 0.0;

        std::cout << std::fixed << std::setprecision(6)
                  << "MODULE_MVOLE_PPRF " << label
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " output_elems=" << outputElems
                  << " total_blocks=" << timing.totalBlocks
                  << " block_size=" << timing.blockSize
                  << " expanded_leaves_per_sparse_vector=" << timing.expandedLeavesPerSparseVector
                  << " expected_N=" << timing.expectedLeavesPerSparseVector
                  << " expanded_leaves_over_N=" << timing.expandedLeavesOverN
                  << " pprf_s_setup_s=" << timing.pprfSSeconds
                  << " pprf_e_setup_s=" << timing.pprfESeconds
                  << " expand_p0_s=" << timing.expandP0Seconds
                  << " expand_p1_s=" << timing.expandP1Seconds
                  << " verify_s=" << timing.verifySeconds
                  << " total_s=" << total
                  << " throughput_elems_per_s=" << throughput
                  << " shared_path=1"
                  << " " << (timing.ok ? "PASS" : "FAIL")
                  << std::endl;

        return timing.ok;
    }

    inline ModuleMVOLEParams moduleMvolePprfPreset(u64 preset)
    {
        ModuleMVOLEParams params;

        if (preset == 1)
        {
            params.n = 10;
            params.t = 8;
            params.m = 8;
        }
        else if (preset == 2)
        {
            params.n = 12;
            params.t = 16;
            params.m = 16;
        }
        else if (preset == 3)
        {
            params.n = 14;
            params.t = 32;
            params.m = 16;
        }
        else if (preset == 4)
        {
            params.n = 16;
            params.t = 64;
            params.m = 16;
        }
        else if (preset == 5)
        {
            params.n = 18;
            params.t = 128;
            params.m = 16;
        }
        else if (preset == 6)
        {
            params.n = 16;
            params.t = 64;
            params.m = 32;
        }
        else if (preset == 7)
        {
            params.n = 18;
            params.t = 128;
            params.m = 32;
        }
        else
        {
            params.n = 10;
            params.t = 8;
            params.m = 8;
        }

        params.N = 1ull << params.n;
        return params;
    }

    inline int ModuleMVOLE_Pprf(u64 preset)
    {
        auto params = moduleMvolePprfPreset(preset);
        auto ok64 = runModuleMvolePprf<u64, CoeffCtxIntegerPrime_64>(params, "Fp64");
        return ok64 ? 0 : 1;
    }
}
