#pragma once

#include "ModuleMvolePprfIntegration.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace osuCrypto
{
    struct ModuleMVOLECoordPprfTiming
    {
        double pprfSSeconds = 0;
        double pprfESeconds = 0;
        double expandP0Seconds = 0;
        double expandP1Seconds = 0;
        double verifySeconds = 0;
        double totalSeconds = 0;
        u64 totalBlocks = 0;
        u64 blockSize = 0;
        u64 expandedLeavesPerCoordinate = 0;
        u64 expandedScalarLeavesPerSparseVector = 0;
        double expandedScalarLeavesOverN = 0;
        bool ok = false;
    };

    template<typename F, typename Ctx>
    u64 moduleMvoleRegularSparseOffset(
        const ModuleMVOLEParams& params,
        const typename Ctx::template Vec<F>& sparse,
        u64 blockIdx,
        Ctx& ctx)
    {
        auto blockSize = moduleMvoleRegularBlockSize(params);
        auto blockStart = blockIdx * blockSize;
        F zero{};

        for (u64 j = 0; j < blockSize; ++j)
        {
            if (!ctx.eq(sparse[blockStart + j], zero))
                return j;
        }

        throw std::runtime_error("MODULE_MVOLE_COORD_PPRF regular sparse block has no nonzero coefficient.");
    }

    template<typename F, typename Ctx>
    u64 moduleMvoleExpandRegularCoordPprfIntoRows(
        const ModuleMVOLEParams& params,
        const typename Ctx::template Vec<F>& sparse,
        const typename Ctx::template Vec<F>& Delta,
        std::vector<ModuleMVOLERowMask<F, Ctx>>& rowMasks,
        bool useSRows,
        PRNG& prng,
        Ctx& ctx)
    {
        auto blockSize = moduleMvoleRegularBlockSize(params);
        AES aes(block(0x4d56435052464c45ull, 0x434f4f5244534331ull));
        std::vector<block> leaves;
        leaves.reserve(blockSize);
        u64 expandedLeaves = 0;

        // Coordinate-wise semantic baseline: for each coordinate h, run a
        // separate scalar regular-block PPRF over every block. The sparse
        // support is shared with RM-VOLE, but tree/path work is not shared
        // across the m coordinates.
        for (u64 h = 0; h < params.m; ++h)
        {
            auto& share0 = useSRows ? rowMasks[h].V : rowMasks[h].U;
            auto& share1 = useSRows ? rowMasks[h].W0 : rowMasks[h].W1;

            for (u64 i = 0; i < params.t; ++i)
            {
                auto offset = moduleMvoleRegularSparseOffset<F, Ctx>(params, sparse, i, ctx);
                auto blockStart = i * blockSize;
                auto globalAlpha = blockStart + offset;
                auto coeff = sparse[globalAlpha];

                block rootSeed = prng.get<block>();
                moduleVectorPprfExpandSharedTree(rootSeed, blockSize, leaves);
                expandedLeaves += blockSize;

                for (u64 j = 0; j < blockSize; ++j)
                {
                    auto globalIdx = blockStart + j;
                    F value;
                    ctx.fromBlock(value, aes.hashBlock(leaves[j] ^ block(i, h)));

                    moduleMvoleAddToVec<F, Ctx>(share0, globalIdx, value, ctx);
                    moduleMvoleAddToVec<F, Ctx>(share1, globalIdx, moduleVectorPprfNeg<F, Ctx>(value, ctx), ctx);
                }

                F beta;
                ctx.mul(beta, Delta[h], coeff);
                moduleMvoleAddToVec<F, Ctx>(share1, globalAlpha, beta, ctx);
            }
        }

        return expandedLeaves;
    }

    template<typename F, typename Ctx>
    void moduleMvoleGenCoordPprfSetup(
        ModuleMVOLEGenState<F, Ctx>& gen,
        const ModuleMVOLEParams& params,
        ModuleMVOLECoordPprfTiming& timing,
        PRNG& prng,
        Ctx ctx = {})
    {
        auto blockSize = moduleMvoleRegularBlockSize(params);
        timing.totalBlocks = params.t;
        timing.blockSize = blockSize;
        timing.expandedLeavesPerCoordinate = params.N;

        gen.base = moduleMvoleGenBaseRegular<F, Ctx>(params, prng, ctx);
        moduleMvoleGenDelta<F, Ctx>(gen.Delta, params, prng, ctx);
        moduleMvoleInitPprfRows<F, Ctx>(params, gen.rowMasks, ctx);

        auto start = omp_get_wtime();
        auto expandedS = moduleMvoleExpandRegularCoordPprfIntoRows<F, Ctx>(
            params, gen.base.s, gen.Delta, gen.rowMasks, true, prng, ctx);
        auto end = omp_get_wtime();
        timing.pprfSSeconds = end - start;

        start = omp_get_wtime();
        auto expandedE = moduleMvoleExpandRegularCoordPprfIntoRows<F, Ctx>(
            params, gen.base.e, gen.Delta, gen.rowMasks, false, prng, ctx);
        end = omp_get_wtime();
        timing.pprfESeconds = end - start;

        timing.expandedScalarLeavesPerSparseVector = expandedS;
        if (expandedS != expandedE)
            throw std::runtime_error("MODULE_MVOLE_COORD_PPRF regular s/e expanded leaf counts differ.");
        timing.expandedScalarLeavesOverN = params.N
            ? static_cast<double>(expandedS) / static_cast<double>(params.N)
            : 0;
    }

    template<typename F, typename Ctx>
    ModuleMVOLECoordPprfTiming moduleMvoleCoordPprfRunOnce(
        const ModuleMVOLEParams& params,
        Ctx ctx = {})
    {
        PRNG prng(sysRandomSeed());
        ModuleMVOLEGenState<F, Ctx> gen;
        ModuleMVOLEResult<F, Ctx> result;
        ModuleMVOLECoordPprfTiming timing;

        auto totalStart = omp_get_wtime();
        moduleMvoleGenCoordPprfSetup<F, Ctx>(gen, params, timing, prng, ctx);

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

    inline double moduleMvoleCoordPprfTotalSeconds(const ModuleMVOLECoordPprfTiming& timing)
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
    bool runModuleMvoleCoordPprf(const ModuleMVOLEParams& params, const std::string& label)
    {
        auto timing = moduleMvoleCoordPprfRunOnce<F, Ctx>(params);
        auto total = moduleMvoleCoordPprfTotalSeconds(timing);
        auto outputElems = params.m * params.N;
        auto throughput = total > 0 ? static_cast<double>(outputElems) / total : 0.0;

        std::cout << std::fixed << std::setprecision(6)
                  << "MODULE_MVOLE_COORD_PPRF " << label
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " output_elems=" << outputElems
                  << " total_blocks=" << timing.totalBlocks
                  << " block_size=" << timing.blockSize
                  << " expanded_leaves_per_coordinate=" << timing.expandedLeavesPerCoordinate
                  << " expanded_scalar_leaves_per_sparse_vector=" << timing.expandedScalarLeavesPerSparseVector
                  << " expected_mN=" << outputElems
                  << " expanded_scalar_leaves_over_N=" << timing.expandedScalarLeavesOverN
                  << " pprf_s_setup_s=" << timing.pprfSSeconds
                  << " pprf_e_setup_s=" << timing.pprfESeconds
                  << " expand_p0_s=" << timing.expandP0Seconds
                  << " expand_p1_s=" << timing.expandP1Seconds
                  << " verify_s=" << timing.verifySeconds
                  << " total_s=" << total
                  << " throughput_elems_per_s=" << throughput
                  << " shared_path=0"
                  << " " << (timing.ok ? "PASS" : "FAIL")
                  << std::endl;

        return timing.ok;
    }

    inline int ModuleMVOLE_CoordPprf(u64 preset)
    {
        auto params = moduleMvolePprfPreset(preset);
        auto ok64 = runModuleMvoleCoordPprf<u64, CoeffCtxIntegerPrime_64>(params, "Fp64");
        return ok64 ? 0 : 1;
    }
}
